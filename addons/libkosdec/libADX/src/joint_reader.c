#include <kosdec/adx_joint_reader.h>
#define READER_MAGIC UINT32_C(0x4144584b)
static int valid(const struct kosdec_adx_joint_reader *r) {
    return r && r->magic == READER_MAGIC;
}
enum kosdec_adx_joint_status kosdec_adx_joint_reader_init(
    struct kosdec_adx_joint_reader *r, struct kosdec_adx_joint *j) {
    if(!r) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    struct kosdec_adx_joint_info info;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_query(j, &info);
    if(s != KOSDEC_ADX_JOINT_OK) return s;
    if(info.borrowed[KOSDEC_ADX_JOINT_DATA]) return KOSDEC_ADX_JOINT_BUSY;
    *r = (struct kosdec_adx_joint_reader){0};
    r->magic = READER_MAGIC; r->joint = j; kosdec_adx_init(&r->decoder);
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_filter(
    struct kosdec_adx_joint_reader *r, kosdec_adx_pcm_filter fn, void *context) {
    if(!valid(r)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->terminal) return r->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : r->terminal;
    return kosdec_adx_set_filter(&r->decoder, fn, context);
}
static enum kosdec_adx_joint_status release(struct kosdec_adx_joint_reader *r) {
    if(!r->held.bytes) return KOSDEC_ADX_JOINT_OK;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_put(r->joint, &r->held);
    if(s == KOSDEC_ADX_JOINT_OK) {
        r->held = (struct kosdec_adx_chunk){0}; r->used = 0;
    }
    return s;
}
struct kosdec_adx_joint_read_result kosdec_adx_joint_reader_read(
    struct kosdec_adx_joint_reader *r, int16_t *output, size_t frames) {
    struct kosdec_adx_joint_read_result result = {
        KOSDEC_ADX_BAD_ARGUMENT, KOSDEC_ADX_JOINT_OK, 0};
    if(!valid(r) || (!output && frames) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return result;
    if(r->terminal) {
        result.status = r->terminal; result.joint_status = r->joint_error; return result;
    }
    if(!frames) { result.status = KOSDEC_ADX_NEED_OUTPUT; return result; }
    struct kosdec_adx_joint_info info;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_query(r->joint, &info);
    if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    if(!r->held.bytes) {
        size_t request = KOSDEC_ADX_HEADER_BUDGET;
        if(info.kind == KOSDEC_ADX_JOINT_QUEUE &&
           request < info.contiguous[KOSDEC_ADX_JOINT_DATA])
            request = info.contiguous[KOSDEC_ADX_JOINT_DATA];
        s = kosdec_adx_joint_get(r->joint, KOSDEC_ADX_JOINT_DATA, request, &r->held);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        info.available[KOSDEC_ADX_JOINT_DATA] -= r->held.bytes;
    }
    struct kosdec_adx_result decoded = kosdec_adx_decode(&r->decoder,
        r->held.bytes ? r->held.data + r->used : NULL, r->held.bytes - r->used,
        output, frames, info.sealed && !info.available[KOSDEC_ADX_JOINT_DATA]);
    r->used += decoded.consumed; r->consumed_bytes += decoded.consumed;
    result.frames = decoded.frames; result.status = decoded.status;
    if(r->used == r->held.bytes) {
        s = release(r);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    }
    if(decoded.status >= KOSDEC_ADX_DONE) r->terminal = decoded.status;
    return result;
joint_failure:
    r->terminal = KOSDEC_ADX_IO_ERROR; r->joint_error = s;
    result.status = r->terminal; result.joint_status = s;
    return result;
}
void kosdec_adx_joint_reader_cancel(struct kosdec_adx_joint_reader *r) {
    if(valid(r) && !r->terminal) {
        kosdec_adx_cancel(&r->decoder); r->terminal = KOSDEC_ADX_CANCELLED;
    }
}
enum kosdec_adx_joint_status kosdec_adx_joint_reader_close(struct kosdec_adx_joint_reader *r) {
    if(!valid(r)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    enum kosdec_adx_joint_status s = release(r);
    if(s == KOSDEC_ADX_JOINT_OK) *r = (struct kosdec_adx_joint_reader){0};
    return s;
}
