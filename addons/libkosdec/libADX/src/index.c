/* Independent byte-safe index parsing. No proprietary tables or code. */
#include <kosdec/adx_index.h>
#include <string.h>
#define INDEX_MAGIC UINT32_C(0x41445849)
#define VIEW_MAGIC UINT32_C(0x41445856)
enum kosdec_adx_status kosdec_adx_view_init(struct kosdec_adx_view *view,
    kosdec_adx_read_at read_at, void *context, uint32_t input_bytes,
    uint32_t offset, uint32_t bytes) {
    if(!view || !read_at || offset > input_bytes || bytes > input_bytes - offset)
        return KOSDEC_ADX_BAD_ARGUMENT;
    *view = (struct kosdec_adx_view){VIEW_MAGIC, offset, bytes, read_at, context};
    return KOSDEC_ADX_DONE;
}
static uint32_t word(const uint8_t *p, enum kosdec_adx_container format) {
    if(format == KOSDEC_ADX_ACX)
        return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
               (uint32_t)p[2] << 8 | p[3];
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
           (uint32_t)p[1] << 8 | p[0];
}
void kosdec_adx_index_init(struct kosdec_adx_index *s, kosdec_adx_read_at read_at,
    void *context, uint32_t bytes, enum kosdec_adx_container format, uint32_t ordinal) {
    if(!s) return;
    memset(s, 0, sizeof(*s));
    s->magic = INDEX_MAGIC; s->read_at = read_at; s->context = context;
    s->input_bytes = bytes; s->format = format; s->ordinal = ordinal;
    if(!read_at || (format != KOSDEC_ADX_ACX && format != KOSDEC_ADX_AFS))
        s->terminal = KOSDEC_ADX_BAD_ARGUMENT;
    else if(bytes < 8) s->terminal = KOSDEC_ADX_TRUNCATED;
}
void kosdec_adx_index_cancel(struct kosdec_adx_index *s) {
    if(s && s->magic == INDEX_MAGIC && !s->terminal)
        s->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_index_step(struct kosdec_adx_index *s) {
    if(!s || s->magic != INDEX_MAGIC) return KOSDEC_ADX_BAD_ARGUMENT;
    if(s->terminal) return s->terminal;
    size_t request = sizeof(s->scratch) - s->used;
    uint32_t position = (s->header_ready ? s->entry_offset : 0) + s->used;
    struct kosdec_adx_input_result in = s->read_at(s->context, position,
        s->scratch + s->used, request);
    if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) return KOSDEC_ADX_NEED_INPUT;
    if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > request) {
        s->terminal = in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
            KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
        return s->terminal;
    }
    s->used += (unsigned)in.bytes;
    if(s->used != sizeof(s->scratch)) return KOSDEC_ADX_MORE;
    if(!s->header_ready) {
        if(s->format == KOSDEC_ADX_ACX) {
            if(word(s->scratch, s->format)) {
                s->terminal = KOSDEC_ADX_UNSUPPORTED; return s->terminal;
            }
        }
        else {
            if(memcmp(s->scratch, "AFS", 3)) {
                s->terminal = KOSDEC_ADX_INVALID; return s->terminal;
            }
            if(s->scratch[3]) {
                s->terminal = KOSDEC_ADX_UNSUPPORTED; return s->terminal;
            }
        }
        s->count = word(s->scratch + 4, s->format);
        if(s->count > (s->input_bytes - 8u) / 8u) {
            s->terminal = KOSDEC_ADX_INVALID; return s->terminal;
        }
        s->table_end = 8u + 8u * s->count;
        s->header_ready = true;
        if(s->ordinal >= s->count) {
            s->terminal = KOSDEC_ADX_NOT_FOUND; return s->terminal;
        }
        s->entry_offset = 8u + 8u * s->ordinal;
        s->used = 0;
        return KOSDEC_ADX_MORE;
    }
    uint32_t offset = word(s->scratch, s->format);
    uint32_t bytes = word(s->scratch + 4, s->format);
    if(!bytes) s->terminal = KOSDEC_ADX_UNSUPPORTED;
    else if(offset < s->table_end || offset > s->input_bytes ||
            bytes > s->input_bytes - offset) s->terminal = KOSDEC_ADX_INVALID;
    else {
        s->member = (struct kosdec_adx_view){VIEW_MAGIC, offset, bytes,
                                           s->read_at, s->context};
        s->terminal = KOSDEC_ADX_DONE;
    }
    return s->terminal;
}
struct kosdec_adx_input_result kosdec_adx_view_read_at(
    void *context, uint32_t offset, uint8_t *dst, size_t capacity) {
    const struct kosdec_adx_view *v = context;
    struct kosdec_adx_input_result error = {KOSDEC_ADX_INPUT_ERROR, 0};
    if(!v || v->magic != VIEW_MAGIC || !dst || !capacity || offset > v->bytes)
        return error;
    if(offset == v->bytes)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_END, 0};
    size_t request = v->bytes - offset;
    if(request > capacity) request = capacity;
    if(request > 512u) request = 512u;
    struct kosdec_adx_input_result in = v->read_at(v->context,
        v->offset + offset, dst, request);
    if(in.status == KOSDEC_ADX_INPUT_DATA)
        return in.bytes && in.bytes <= request ? in : error;
    if(!in.bytes && (in.status == KOSDEC_ADX_INPUT_WAIT ||
                    in.status == KOSDEC_ADX_INPUT_END ||
                    in.status == KOSDEC_ADX_INPUT_ERROR)) return in;
    return error;
}
