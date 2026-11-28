#include <kosdec/acx.h>

enum { HEADER_BYTES = 8, ENTRY_BYTES = 8 };

static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static enum kosdec_acx_status read_exact(const struct kosdec_acx *archive,
    uint64_t position, void *destination, size_t bytes) {
    if(position > archive->bytes || bytes > archive->bytes - position)
        return KOSDEC_ACX_TRUNCATED;
    uint8_t *out = destination;
    while(bytes) {
        size_t request = bytes > PTRDIFF_MAX ? PTRDIFF_MAX : bytes;
        ptrdiff_t got = archive->read_at(archive->context, position, out, request);
        if(got < 0 || (size_t)got > request)
            return KOSDEC_ACX_IO_ERROR;
        if(!got)
            return KOSDEC_ACX_TRUNCATED;
        out += got;
        position += (size_t)got;
        bytes -= (size_t)got;
    }
    return KOSDEC_ACX_OK;
}

static uint64_t entry_position(uint32_t id) {
    return HEADER_BYTES + (uint64_t)id * ENTRY_BYTES;
}

static enum kosdec_acx_status parse_entry(const struct kosdec_acx *archive,
    uint32_t id, const uint8_t *data, struct kosdec_acx_member *out) {
    uint32_t offset = word(data), bytes = word(data + 4);
    if(offset < entry_position(archive->count) || offset > archive->bytes ||
       bytes > archive->bytes - offset)
        return KOSDEC_ACX_INVALID;
    if(out)
        *out = (struct kosdec_acx_member){archive, id, offset, bytes};
    return KOSDEC_ACX_OK;
}

enum kosdec_acx_status kosdec_acx_open(struct kosdec_acx *out,
    kosdec_acx_read_at read_at, void *context, uint64_t bytes) {
    if(!out || !read_at)
        return KOSDEC_ACX_BAD_ARGUMENT;
    struct kosdec_acx archive = {.read_at = read_at, .context = context, .bytes = bytes};
    uint8_t rows[512];
    enum kosdec_acx_status status = read_exact(&archive, 0, rows, HEADER_BYTES);
    if(status != KOSDEC_ACX_OK)
        return status;
    if(word(rows))
        return KOSDEC_ACX_UNSUPPORTED;
    archive.count = word(rows + 4);
    if(entry_position(archive.count) > bytes)
        return KOSDEC_ACX_TRUNCATED;

    for(uint32_t id = 0; id < archive.count; ) {
        uint32_t batch = archive.count - id;
        if(batch > sizeof(rows) / ENTRY_BYTES)
            batch = sizeof(rows) / ENTRY_BYTES;
        status = read_exact(&archive, entry_position(id), rows, batch * ENTRY_BYTES);
        if(status != KOSDEC_ACX_OK)
            return status;
        for(uint32_t i = 0; i < batch; ++i) {
            status = parse_entry(&archive, id + i, rows + i * ENTRY_BYTES, NULL);
            if(status != KOSDEC_ACX_OK)
                return status;
        }
        id += batch;
    }
    *out = archive;
    return KOSDEC_ACX_OK;
}

enum kosdec_acx_status kosdec_acx_get(const struct kosdec_acx *archive,
    uint32_t id, struct kosdec_acx_member *out) {
    if(!archive || !archive->read_at || !out)
        return KOSDEC_ACX_BAD_ARGUMENT;
    if(id >= archive->count)
        return KOSDEC_ACX_NOT_FOUND;
    uint8_t entry[ENTRY_BYTES];
    enum kosdec_acx_status status = read_exact(archive, entry_position(id), entry, sizeof(entry));
    return status == KOSDEC_ACX_OK ? parse_entry(archive, id, entry, out) : status;
}

enum kosdec_acx_status kosdec_acx_read(const struct kosdec_acx_member *member,
    uint32_t position, void *destination, size_t capacity, size_t *used) {
    if(!used)
        return KOSDEC_ACX_BAD_ARGUMENT;
    *used = 0;
    if(!member || !member->archive || !member->archive->read_at ||
       (!destination && capacity) || position > member->bytes)
        return KOSDEC_ACX_BAD_ARGUMENT;
    if(capacity && position == member->bytes)
        return KOSDEC_ACX_EOF;
    size_t bytes = member->bytes - position;
    if(bytes > capacity)
        bytes = capacity;
    enum kosdec_acx_status status = read_exact(member->archive,
        (uint64_t)member->offset + position, destination, bytes);
    if(status == KOSDEC_ACX_OK)
        *used = bytes;
    return status;
}
