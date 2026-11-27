#include <kosdec/afs.h>

#include <string.h>

enum { HEADER = 8, ENTRY = 8, NAME_RECORD = KOSDEC_AFS_NAME_BYTES + KOSDEC_AFS_METADATA_BYTES };

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
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
    uint64_t last_end) {
    uint8_t pointer[ENTRY];
    enum kosdec_afs_status status = read_exact(afs, location, pointer, sizeof(pointer));
    if(status != KOSDEC_AFS_OK)
        return status;
    uint32_t offset = word(pointer), bytes = word(pointer + 4);
    if(!bytes)
        return KOSDEC_AFS_OK;
    if(offset < last_end || offset > afs->bytes ||
       bytes > afs->bytes - offset || bytes < (uint64_t)afs->count * NAME_RECORD)
        return KOSDEC_AFS_INVALID;
    afs->names_offset = offset;
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
    if(memcmp(header, "AFS", 3))
        return KOSDEC_AFS_INVALID;
    if(header[3])
        return KOSDEC_AFS_UNSUPPORTED;

    afs.count = word(header + 4);
    uint64_t table_end = HEADER + (uint64_t)afs.count * ENTRY;
    if(table_end > bytes)
        return KOSDEC_AFS_TRUNCATED;
    uint64_t first = bytes, last_end = table_end;
    uint8_t entries[512];
    for(uint64_t at = HEADER; at < table_end; ) {
        size_t batch = table_end - at < sizeof(entries) ? (size_t)(table_end - at) : sizeof(entries);
        status = read_exact(&afs, at, entries, batch);
        if(status != KOSDEC_AFS_OK)
            return status;
        for(size_t i = 0; i < batch; i += ENTRY) {
            uint32_t offset = word(entries + i), size = word(entries + i + 4);
            if(!offset && !size)
                continue;
            if(offset < table_end || offset > bytes || size > bytes - offset)
                return KOSDEC_AFS_INVALID;
            uint64_t end = (uint64_t)offset + size;
            if(offset < first)
                first = offset;
            if(end > last_end)
                last_end = end;
        }
        at += batch;
    }

    if(afs.count && first - table_end >= ENTRY) {
        status = name_pointer(&afs, table_end, last_end);
        if(status == KOSDEC_AFS_OK && !afs.names_offset && first < bytes && first - table_end > ENTRY)
            status = name_pointer(&afs, first - ENTRY, last_end);
        if(status != KOSDEC_AFS_OK)
            return status;
    }
    *archive = afs;
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
        .archive = afs, .id = id, .offset = word(row), .bytes = word(row + 4),
        .has_name_table = afs->names_offset != 0
    };
    if(member.has_name_table) {
        status = read_exact(afs, afs->names_offset + (uint64_t)id * NAME_RECORD, row, sizeof(row));
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
    for(uint32_t id = 0; id < afs->count; ++id) {
        char candidate[KOSDEC_AFS_NAME_BYTES + 1] = {0};
        enum kosdec_afs_status status = read_exact(afs,
            afs->names_offset + (uint64_t)id * NAME_RECORD, candidate, KOSDEC_AFS_NAME_BYTES);
        if(status != KOSDEC_AFS_OK)
            return status;
        if(!strcmp(candidate, name))
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
