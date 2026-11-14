#include <kosdec/adx_request.h>
#include <string.h>
#define REQUEST_MAGIC UINT32_C(0x41445849)
#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
static bool valid(const struct kosdec_adx_request *r) {
    return r && r->magic == REQUEST_MAGIC;
}
enum kosdec_adx_request_status kosdec_adx_request_init(struct kosdec_adx_request *r,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes) {
    if(!r || !read_at) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    memset(r, 0, sizeof(*r));
    r->magic = REQUEST_MAGIC; r->read_at = read_at; r->context = context;
    r->input_bytes = bytes; r->sectors = bytes / SECTOR + (bytes % SECTOR != 0);
    return r->state = KOSDEC_ADX_REQUEST_STOPPED;
}
enum kosdec_adx_request_status kosdec_adx_request_start(struct kosdec_adx_request *r,
    uint32_t sectors, uint8_t *output, size_t capacity) {
    if(!valid(r)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(r->state == KOSDEC_ADX_REQUEST_READING) return KOSDEC_ADX_REQUEST_BUSY;
    uint32_t remaining = r->sectors - r->cursor;
    if(sectors > remaining) sectors = remaining;
    uint64_t bytes = (uint64_t)sectors * SECTOR;
    if(bytes > capacity || (bytes && !output)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    r->start = r->cursor; r->requested = sectors; r->source_bytes = 0; r->completed = 0;
    r->joint = NULL;
    r->written = 0; r->output_bytes = (size_t)bytes; r->output = sectors ? output : NULL;
    return r->state = sectors ? KOSDEC_ADX_REQUEST_READING : KOSDEC_ADX_REQUEST_READEND;
}
enum kosdec_adx_request_status kosdec_adx_request_start_joint(struct kosdec_adx_request *r,
    uint32_t sectors, struct kosdec_adx_joint *joint) {
    if(!valid(r)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(r->state == KOSDEC_ADX_REQUEST_READING) return KOSDEC_ADX_REQUEST_BUSY;
    struct kosdec_adx_joint_info info;
    if(kosdec_adx_joint_query(joint, &info) != KOSDEC_ADX_JOINT_OK || info.sealed ||
       info.kind == KOSDEC_ADX_JOINT_MEMORY) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(info.borrowed[KOSDEC_ADX_JOINT_FREE]) return KOSDEC_ADX_REQUEST_BUSY;
    uint32_t remaining = r->sectors - r->cursor;
    if(sectors > remaining) sectors = remaining;
    uint64_t bytes = (uint64_t)sectors * SECTOR;
    uint64_t start = (uint64_t)r->cursor * SECTOR;
    uint32_t available = start < r->input_bytes ? r->input_bytes - (uint32_t)start : 0;
    if(bytes > available) bytes = available;
    if(bytes > SIZE_MAX) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    r->start = r->cursor; r->requested = sectors; r->source_bytes = 0; r->completed = 0;
    r->written = 0; r->output_bytes = (size_t)bytes; r->output = NULL;
    r->joint = sectors ? joint : NULL; r->held = (struct kosdec_adx_chunk){0}; r->held_used = 0;
    return r->state = sectors ? KOSDEC_ADX_REQUEST_READING : KOSDEC_ADX_REQUEST_READEND;
}
static bool release(struct kosdec_adx_request *r) {
    if(r->held.bytes && kosdec_adx_joint_unget(r->joint, &r->held) != KOSDEC_ADX_JOINT_OK)
        return false;
    r->held = (struct kosdec_adx_chunk){0}; r->held_used = 0;
    return true;
}
static enum kosdec_adx_request_status finish(struct kosdec_adx_request *r,
                                            enum kosdec_adx_request_status status) {
    if(!release(r)) return KOSDEC_ADX_REQUEST_IO_ERROR;
    if(status == KOSDEC_ADX_REQUEST_READEND) r->completed = r->requested;
    r->cursor = r->start + r->completed;
    r->output = NULL; r->joint = NULL;
    return r->state = status;
}
static enum kosdec_adx_request_status step_joint(struct kosdec_adx_request *r) {
    size_t remaining = r->output_bytes - r->written;
    if(!r->held.bytes) {
        struct kosdec_adx_joint_info info;
        if(kosdec_adx_joint_query(r->joint, &info) != KOSDEC_ADX_JOINT_OK || info.sealed)
            return finish(r, KOSDEC_ADX_REQUEST_IO_ERROR);
        size_t request = remaining < 512 ? remaining : 512;
        if(info.kind == KOSDEC_ADX_JOINT_QUEUE && info.mode == KOSDEC_ADX_JOINT_SEPARATE)
            request = info.contiguous[KOSDEC_ADX_JOINT_FREE];
        enum kosdec_adx_joint_status js = kosdec_adx_joint_get(r->joint,
            KOSDEC_ADX_JOINT_FREE, request, &r->held);
        if(js == KOSDEC_ADX_JOINT_LIMIT) return KOSDEC_ADX_REQUEST_NEED_OUTPUT;
        if(js != KOSDEC_ADX_JOINT_OK) return finish(r, KOSDEC_ADX_REQUEST_IO_ERROR);
        if(!r->held.bytes) return KOSDEC_ADX_REQUEST_NEED_OUTPUT;
    }
    size_t target = r->held.bytes < remaining ? r->held.bytes : remaining;
    if(r->held_used < target) {
        size_t n = target - r->held_used;
        if(n > 512) n = 512;
        uint64_t position = (uint64_t)r->start * SECTOR + r->written + r->held_used;
        struct kosdec_adx_input_result in = r->read_at(r->context, (uint32_t)position,
                                                     r->held.data + r->held_used, n);
        if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) return KOSDEC_ADX_REQUEST_WAIT;
        if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > n)
            return finish(r, in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
                KOSDEC_ADX_REQUEST_TRUNCATED : KOSDEC_ADX_REQUEST_IO_ERROR);
        r->held_used += in.bytes; r->source_bytes += (uint32_t)in.bytes;
        if(r->held_used < target) return KOSDEC_ADX_REQUEST_READING;
    }
    if(target < r->held.bytes) {
        struct kosdec_adx_chunk prefix, tail;
        (void)kosdec_adx_chunk_split(&r->held, target, &prefix, &tail);
        enum kosdec_adx_joint_status js = kosdec_adx_joint_unget(r->joint, &tail);
        if(js == KOSDEC_ADX_JOINT_LIMIT) return KOSDEC_ADX_REQUEST_NEED_OUTPUT;
        if(js != KOSDEC_ADX_JOINT_OK) return KOSDEC_ADX_REQUEST_IO_ERROR;
        r->held = prefix;
    }
    /* Full remaining borrow publication uses its reserved queue node. */
    if(kosdec_adx_joint_put(r->joint, &r->held) != KOSDEC_ADX_JOINT_OK)
        return KOSDEC_ADX_REQUEST_IO_ERROR;
    r->written += r->held.bytes; r->completed = (uint32_t)(r->written / SECTOR);
    r->held = (struct kosdec_adx_chunk){0}; r->held_used = 0;
    return r->written == r->output_bytes ? finish(r, KOSDEC_ADX_REQUEST_READEND) :
                                          KOSDEC_ADX_REQUEST_READING;
}
enum kosdec_adx_request_status kosdec_adx_request_step(struct kosdec_adx_request *r) {
    if(!valid(r)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(r->state != KOSDEC_ADX_REQUEST_READING) return r->state;
    if(r->joint) return step_joint(r);
    size_t n = r->output_bytes - r->written;
    if(n > 512) n = 512;
    uint64_t position = (uint64_t)r->start * SECTOR + r->written;
    if(position < r->input_bytes) {
        uint32_t available = r->input_bytes - (uint32_t)position;
        if(n > available) n = available;
        struct kosdec_adx_input_result in = r->read_at(r->context, (uint32_t)position,
                                                     r->output + r->written, n);
        if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) return KOSDEC_ADX_REQUEST_WAIT;
        if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > n)
            return finish(r, in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
                KOSDEC_ADX_REQUEST_TRUNCATED : KOSDEC_ADX_REQUEST_IO_ERROR);
        n = in.bytes; r->source_bytes += (uint32_t)n;
    } else memset(r->output + r->written, 0, n);
    r->written += n;
    r->completed = (uint32_t)(r->written / SECTOR);
    return r->written == r->output_bytes ? finish(r, KOSDEC_ADX_REQUEST_READEND) :
                                          KOSDEC_ADX_REQUEST_READING;
}
enum kosdec_adx_request_status kosdec_adx_request_stop(struct kosdec_adx_request *r) {
    if(!valid(r)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(!release(r)) return KOSDEC_ADX_REQUEST_IO_ERROR;
    r->output = NULL; r->joint = NULL;
    return r->state = KOSDEC_ADX_REQUEST_STOPPED;
}
enum kosdec_adx_request_status kosdec_adx_request_seek(struct kosdec_adx_request *r,
    int64_t offset, enum kosdec_adx_request_origin origin) {
    if(!valid(r) || (origin != KOSDEC_ADX_REQUEST_SET && origin != KOSDEC_ADX_REQUEST_CUR &&
                     origin != KOSDEC_ADX_REQUEST_END)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    uint32_t base = origin == KOSDEC_ADX_REQUEST_SET ? 0 :
                    origin == KOSDEC_ADX_REQUEST_CUR ? r->cursor : r->sectors;
    if(kosdec_adx_request_stop(r) != KOSDEC_ADX_REQUEST_STOPPED)
        return KOSDEC_ADX_REQUEST_IO_ERROR;
    if(offset < -(int64_t)base) r->cursor = 0;
    else if(offset > (int64_t)(r->sectors - base)) r->cursor = r->sectors;
    else r->cursor = (uint32_t)((int64_t)base + offset);
    return KOSDEC_ADX_REQUEST_STOPPED;
}
bool kosdec_adx_request_get_info(const struct kosdec_adx_request *r,
                               struct kosdec_adx_request_info *out) {
    if(!valid(r) || !out) return false;
    *out = (struct kosdec_adx_request_info){r->input_bytes, r->sectors, r->cursor,
        r->start, r->requested, r->completed, r->source_bytes,
        r->written, r->state};
    return true;
}
enum kosdec_adx_request_status kosdec_adx_request_close(struct kosdec_adx_request *r) {
    if(!valid(r)) return KOSDEC_ADX_REQUEST_BAD_ARGUMENT;
    if(!release(r)) return KOSDEC_ADX_REQUEST_IO_ERROR;
    memset(r, 0, sizeof(*r));
    return KOSDEC_ADX_REQUEST_STOPPED;
}
