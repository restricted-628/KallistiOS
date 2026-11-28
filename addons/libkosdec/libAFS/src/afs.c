#include <kosdec/afs.h>

#include <string.h>

enum { HEADER = 8, ENTRY = 8, AFSLNK_POINTER = 512 * 1024 - ENTRY, NAME_RECORD = KOSDEC_AFS_NAME_BYTES + KOSDEC_AFS_METADATA_BYTES };

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static enum kosdec_afs_status parse_header(struct kosdec_afs *afs, const uint8_t *header) {
    if(memcmp(header, "AFS", 3))
        return KOSDEC_AFS_INVALID;
    if(header[3])
        return KOSDEC_AFS_UNSUPPORTED;
    afs->count = word(header + 4);
    return HEADER + (uint64_t)afs->count * ENTRY <= afs->bytes ?
        KOSDEC_AFS_OK : KOSDEC_AFS_TRUNCATED;
}

static bool parse_range(const struct kosdec_afs *afs, const uint8_t *row,
    uint64_t table_end, struct kosdec_afs_range *range) {
    *range = (struct kosdec_afs_range){word(row), word(row + 4)};
    if(!range->offset && !range->bytes)
        return true;
    return range->offset >= table_end && range->offset <= afs->bytes &&
           range->bytes <= afs->bytes - range->offset;
}

static enum kosdec_afs_status read_exact(const struct kosdec_afs *afs, uint64_t at,
    void *out, size_t bytes) {
    if(at > afs->bytes || bytes > afs->bytes - at)
        return KOSDEC_AFS_TRUNCATED;
    uint8_t *dst = out;
    while(bytes) {
        size_t request = bytes > PTRDIFF_MAX ? PTRDIFF_MAX : bytes;
        ptrdiff_t n = afs->read_at(afs->context, at, dst, request);
        if(n < 0 || (size_t)n > request)
            return KOSDEC_AFS_IO_ERROR;
        if(!n)
            return KOSDEC_AFS_TRUNCATED;
        at += (size_t)n;
        bytes -= (size_t)n;
        dst += n;
    }
    return KOSDEC_AFS_OK;
}

static enum kosdec_afs_status name_pointer(struct kosdec_afs *afs, uint64_t location,
    uint64_t last_end, uint32_t populated) {
    uint8_t pointer[ENTRY];
    enum kosdec_afs_status status = read_exact(afs, location, pointer, sizeof(pointer));
    if(status != KOSDEC_AFS_OK)
        return status;
    uint32_t offset = word(pointer), bytes = word(pointer + 4);
    if(!bytes)
        return KOSDEC_AFS_OK;
    if(offset < last_end || offset > afs->bytes || bytes > afs->bytes - offset)
        return KOSDEC_AFS_INVALID;
    /* AFSLNK -add lists names only for populated ordinal slots. */
    uint32_t records = afs->count;
    if(bytes < (uint64_t)records * NAME_RECORD) {
        if(!populated || bytes != (uint64_t)populated * NAME_RECORD)
            return KOSDEC_AFS_INVALID;
        records = populated;
    }
    afs->names_offset = offset;
    afs->name_records = records;
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_open(struct kosdec_afs *archive,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes) {
    if(!archive || !read_at)
        return KOSDEC_AFS_BAD_ARGUMENT;
    struct kosdec_afs afs = {.read_at = read_at, .context = context, .bytes = bytes};
    uint8_t header[HEADER];
    enum kosdec_afs_status status = read_exact(&afs, 0, header, HEADER);
    if(status != KOSDEC_AFS_OK)
        return status;
    status = parse_header(&afs, header);
    if(status != KOSDEC_AFS_OK)
        return status;
    uint64_t table_end = HEADER + (uint64_t)afs.count * ENTRY;
    uint64_t first = bytes, last_end = table_end;
    uint32_t populated = 0;
    uint8_t entries[512];
    for(uint64_t at = HEADER; at < table_end; ) {
        size_t batch = table_end - at < sizeof(entries) ? (size_t)(table_end - at) : sizeof(entries);
        status = read_exact(&afs, at, entries, batch);
        if(status != KOSDEC_AFS_OK)
            return status;
        for(size_t i = 0; i < batch; i += ENTRY) {
            struct kosdec_afs_range range;
            if(!parse_range(&afs, entries + i, table_end, &range))
                return KOSDEC_AFS_INVALID;
            if(!range.offset)
                continue;
            ++populated;
            uint64_t end = (uint64_t)range.offset + range.bytes;
            if(range.offset < first)
                first = range.offset;
            if(end > last_end)
                last_end = end;
        }
        at += batch;
    }

    if(afs.count && first - table_end >= ENTRY) {
        status = name_pointer(&afs, table_end, last_end, populated);
        if(status == KOSDEC_AFS_OK && !afs.names_offset && table_end < AFSLNK_POINTER &&
           first > AFSLNK_POINTER + ENTRY && first < bytes) {
            /* AFSLNK records the pointer at the end of its fixed header,
               even when alignment moves the first member farther out. */
            enum kosdec_afs_status fixed = name_pointer(&afs, AFSLNK_POINTER, last_end, populated);
            if(fixed != KOSDEC_AFS_INVALID)
                status = fixed;
        }
        if(status == KOSDEC_AFS_OK && !afs.names_offset && first < bytes && first - table_end > ENTRY)
            status = name_pointer(&afs, first - ENTRY, last_end, populated);
        if(status != KOSDEC_AFS_OK)
            return status;
    }
    *archive = afs;
    return KOSDEC_AFS_OK;
}

/* Map between logical IDs and the compact name-table order. An explicit
   empty member has a nonzero offset and occupies a name record. */
static enum kosdec_afs_status sparse_name_map(const struct kosdec_afs *afs, uint32_t target,
    bool id_to_record, uint32_t *result) {
    uint32_t limit = id_to_record ? target : afs->count;
    uint32_t record = 0;
    uint8_t rows[512];
    for(uint32_t id = 0; id < limit; ) {
        uint32_t batch = limit - id;
        if(batch > sizeof(rows) / ENTRY)
            batch = sizeof(rows) / ENTRY;
        enum kosdec_afs_status status = read_exact(afs, HEADER + (uint64_t)id * ENTRY,
            rows, batch * ENTRY);
        if(status != KOSDEC_AFS_OK)
            return status;
        for(uint32_t i = 0; i < batch; ++i) {
            if(word(rows + i * ENTRY)) {
                if(!id_to_record && record == target) {
                    *result = id + i;
                    return KOSDEC_AFS_OK;
                }
                ++record;
            }
        }
        id += batch;
    }
    if(!id_to_record)
        return KOSDEC_AFS_INVALID;
    *result = record;
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_get(const struct kosdec_afs *afs, uint32_t id,
    struct kosdec_afs_member *out) {
    if(!afs || !afs->read_at || !out)
        return KOSDEC_AFS_BAD_ARGUMENT;
    if(id >= afs->count)
        return KOSDEC_AFS_NOT_FOUND;
    uint8_t row[NAME_RECORD];
    enum kosdec_afs_status status = read_exact(afs, HEADER + (uint64_t)id * ENTRY, row, ENTRY);
    if(status != KOSDEC_AFS_OK)
        return status;
    struct kosdec_afs_member member = {
        .archive = afs, .id = id, .offset = word(row), .bytes = word(row + 4)
    };
    bool compact = afs->name_records < afs->count;
    member.has_name_table = afs->names_offset && (!compact || member.offset);
    if(member.has_name_table) {
        uint32_t record = id;
        if(compact) {
            status = sparse_name_map(afs, id, true, &record);
            if(status != KOSDEC_AFS_OK)
                return status;
        }
        status = read_exact(afs, afs->names_offset + (uint64_t)record * NAME_RECORD, row, sizeof(row));
        if(status != KOSDEC_AFS_OK)
            return status;
        memcpy(member.name, row, KOSDEC_AFS_NAME_BYTES);
        memcpy(member.metadata, row + KOSDEC_AFS_NAME_BYTES, KOSDEC_AFS_METADATA_BYTES);
    }
    *out = member;
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_find(const struct kosdec_afs *afs, const char *name,
    struct kosdec_afs_member *out) {
    if(!afs || !afs->read_at || !name || !*name || !out)
        return KOSDEC_AFS_BAD_ARGUMENT;
    if(!afs->names_offset || strlen(name) > KOSDEC_AFS_NAME_BYTES)
        return KOSDEC_AFS_NOT_FOUND;
    for(uint32_t record = 0; record < afs->name_records; ++record) {
        char candidate[KOSDEC_AFS_NAME_BYTES + 1] = {0};
        enum kosdec_afs_status status = read_exact(afs,
            afs->names_offset + (uint64_t)record * NAME_RECORD, candidate, KOSDEC_AFS_NAME_BYTES);
        if(status != KOSDEC_AFS_OK)
            return status;
        if(strcmp(candidate, name))
            continue;
        uint32_t id = record;
        if(afs->name_records < afs->count) {
            status = sparse_name_map(afs, record, false, &id);
            if(status != KOSDEC_AFS_OK)
                return status;
        }
        return kosdec_afs_get(afs, id, out);
    }
    return KOSDEC_AFS_NOT_FOUND;
}

enum kosdec_afs_status kosdec_afs_read(const struct kosdec_afs_member *member,
    uint32_t position, void *destination, size_t capacity, size_t *used) {
    if(!used)
        return KOSDEC_AFS_BAD_ARGUMENT;
    *used = 0;
    if(!member || !member->archive || !member->archive->read_at ||
       (!destination && capacity) || position > member->bytes)
        return KOSDEC_AFS_BAD_ARGUMENT;
    if(capacity && position == member->bytes)
        return KOSDEC_AFS_EOF;
    size_t bytes = member->bytes - position < capacity ? member->bytes - position : capacity;
    enum kosdec_afs_status status = read_exact(member->archive,
        (uint64_t)member->offset + position, destination, bytes);
    if(status == KOSDEC_AFS_OK)
        *used = bytes;
    return status;
}

size_t kosdec_afs_partition_work_size(uint32_t count) {
    return count <= KOSDEC_AFS_PARTITION_MAX_MEMBERS ? count * sizeof(struct kosdec_afs_range) : 0;
}

enum kosdec_afs_status kosdec_afs_partition_begin(struct kosdec_afs_partition *p,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes, const char *filename,
    void *directory, struct kosdec_afs_range *entries, uint32_t capacity) {
    if(!p || !read_at || !filename || !*filename || (!entries && capacity) ||
       capacity > KOSDEC_AFS_PARTITION_MAX_MEMBERS)
        return KOSDEC_AFS_BAD_ARGUMENT;
    if(p->bound)
        return KOSDEC_AFS_BUSY;
    *p = (struct kosdec_afs_partition){
        .archive = {.read_at = read_at, .context = context, .bytes = bytes},
        .entries = entries, .filename = filename, .directory = directory,
        .capacity = capacity, .state = bytes < HEADER ? KOSDEC_AFS_TRUNCATED : KOSDEC_AFS_LOADING
    };
    return p->state;
}

enum kosdec_afs_status kosdec_afs_partition_status(const struct kosdec_afs_partition *p) {
    return p && p->archive.read_at ? p->state : KOSDEC_AFS_BAD_ARGUMENT;
}

enum kosdec_afs_status kosdec_afs_partition_step(struct kosdec_afs_partition *p) {
    enum kosdec_afs_status state = kosdec_afs_partition_status(p);
    if(state != KOSDEC_AFS_LOADING && state != KOSDEC_AFS_WAIT)
        return state;

    unsigned target = HEADER;
    uint64_t position = 0;
    if(p->have_header) {
        unsigned remaining = (p->archive.count - p->loaded) * ENTRY;
        target = remaining < sizeof(p->scratch) ? remaining : sizeof(p->scratch);
        position = HEADER + (uint64_t)p->loaded * ENTRY;
    }
    unsigned requested = target - p->used;
    ptrdiff_t received = p->archive.read_at(p->archive.context, position + p->used,
        p->scratch + p->used, requested);
    if(received == KOSDEC_AFS_READ_WAIT)
        return p->state = KOSDEC_AFS_WAIT;
    if(received < 0 || (size_t)received > requested)
        return p->state = KOSDEC_AFS_IO_ERROR;
    if(!received)
        return p->state = KOSDEC_AFS_TRUNCATED;
    p->used += (unsigned)received;
    if(p->used < target)
        return p->state = KOSDEC_AFS_LOADING;
    p->used = 0;
    if(!p->have_header) {
        state = parse_header(&p->archive, p->scratch);
        if(state != KOSDEC_AFS_OK)
            return p->state = state;
        if(p->archive.count > KOSDEC_AFS_PARTITION_MAX_MEMBERS)
            return p->state = KOSDEC_AFS_UNSUPPORTED;
        if(p->archive.count > p->capacity)
            return p->state = KOSDEC_AFS_NO_SPACE;
        p->have_header = true;
    } else {
        uint64_t table_end = HEADER + (uint64_t)p->archive.count * ENTRY;
        for(unsigned i = 0; i < target; i += ENTRY) {
            if(!parse_range(&p->archive, p->scratch + i, table_end, &p->entries[p->loaded]))
                return p->state = KOSDEC_AFS_INVALID;
            ++p->loaded;
        }
    }
    return p->state = p->loaded == p->archive.count ? KOSDEC_AFS_OK : KOSDEC_AFS_LOADING;
}

enum kosdec_afs_status kosdec_afs_partition_load(struct kosdec_afs_partition *p,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes, const char *filename,
    void *directory, struct kosdec_afs_range *entries, uint32_t capacity) {
    enum kosdec_afs_status state = kosdec_afs_partition_begin(p, read_at, context,
        bytes, filename, directory, entries, capacity);
    while(state == KOSDEC_AFS_LOADING)
        state = kosdec_afs_partition_step(p);
    return state;
}

static enum kosdec_afs_status attach_partition(struct kosdec_afs_registry *registry,
    unsigned id, struct kosdec_afs_partition *p, bool overlay) {
    if(!registry || id >= KOSDEC_AFS_PARTITION_IDS)
        return KOSDEC_AFS_BAD_ARGUMENT;
    enum kosdec_afs_status state = kosdec_afs_partition_status(p);
    if(state != KOSDEC_AFS_OK)
        return state;
    struct kosdec_afs_partition *head = registry->heads[id];
    if(!overlay && head == p)
        return KOSDEC_AFS_OK;
    if(p->bound || (!overlay && head))
        return KOSDEC_AFS_BUSY;
    if(overlay && !head)
        return KOSDEC_AFS_NOT_FOUND;
    p->previous = head;
    p->bound = true;
    registry->heads[id] = p;
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_partition_bind(struct kosdec_afs_registry *registry,
    unsigned id, struct kosdec_afs_partition *partition) {
    return attach_partition(registry, id, partition, false);
}

enum kosdec_afs_status kosdec_afs_partition_add(struct kosdec_afs_registry *registry,
    unsigned id, struct kosdec_afs_partition *partition) {
    return attach_partition(registry, id, partition, true);
}

enum kosdec_afs_status kosdec_afs_partition_pop(struct kosdec_afs_registry *registry, unsigned id) {
    if(!registry || id >= KOSDEC_AFS_PARTITION_IDS)
        return KOSDEC_AFS_BAD_ARGUMENT;
    struct kosdec_afs_partition *head = registry->heads[id];
    if(!head)
        return KOSDEC_AFS_NOT_FOUND;
    registry->heads[id] = head->previous;
    head->previous = NULL;
    head->bound = false;
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_partition_info_size(const struct kosdec_afs_registry *registry,
    unsigned id, size_t *bytes) {
    if(!registry || id >= KOSDEC_AFS_PARTITION_IDS || !bytes)
        return KOSDEC_AFS_BAD_ARGUMENT;
    const struct kosdec_afs_partition *head = registry->heads[id];
    if(!head)
        return KOSDEC_AFS_NOT_FOUND;
    *bytes = kosdec_afs_partition_work_size(head->archive.count);
    return KOSDEC_AFS_OK;
}

static const struct kosdec_afs_partition *find_partition(const struct kosdec_afs_registry *registry,
    unsigned partition_id, uint32_t member_id) {
    const struct kosdec_afs_partition *p = registry->heads[partition_id];
    if(!p || member_id >= p->archive.count)
        return NULL;
    for(; p; p = p->previous) {
        if(member_id < p->archive.count && p->entries[member_id].offset)
            return p;
    }
    return NULL;
}

enum kosdec_afs_status kosdec_afs_partition_open(const struct kosdec_afs_registry *registry,
    unsigned partition_id, uint32_t member_id, struct kosdec_afs_member *out) {
    if(!registry || partition_id >= KOSDEC_AFS_PARTITION_IDS || !out)
        return KOSDEC_AFS_BAD_ARGUMENT;
    const struct kosdec_afs_partition *p = find_partition(registry, partition_id, member_id);
    if(!p)
        return KOSDEC_AFS_NOT_FOUND;
    const struct kosdec_afs_range *range = &p->entries[member_id];
    *out = (struct kosdec_afs_member){
        .archive = &p->archive, .id = member_id, .offset = range->offset, .bytes = range->bytes
    };
    return KOSDEC_AFS_OK;
}

enum kosdec_afs_status kosdec_afs_partition_sector_range(const struct kosdec_afs_registry *registry,
    unsigned partition_id, uint32_t member_id, struct kosdec_afs_sector_range *out) {
    if(!registry || partition_id >= KOSDEC_AFS_PARTITION_IDS || !out)
        return KOSDEC_AFS_BAD_ARGUMENT;
    const struct kosdec_afs_partition *p = find_partition(registry, partition_id, member_id);
    if(!p)
        return KOSDEC_AFS_NOT_FOUND;
    const struct kosdec_afs_range *range = &p->entries[member_id];
    if(range->offset % KOSDEC_AFS_SECTOR_BYTES)
        return KOSDEC_AFS_UNSUPPORTED;
    *out = (struct kosdec_afs_sector_range){
        .filename = p->filename, .directory = p->directory,
        .first_sector = range->offset / KOSDEC_AFS_SECTOR_BYTES,
        .sectors = (uint32_t)(((uint64_t)range->bytes + KOSDEC_AFS_SECTOR_BYTES - 1) /
            KOSDEC_AFS_SECTOR_BYTES)
    };
    return KOSDEC_AFS_OK;
}
