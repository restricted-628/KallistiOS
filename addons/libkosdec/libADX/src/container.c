#include "adx_container_internal.h"
#include <string.h>

enum { READ_CALL_LIMIT = 16 };

enum kosdec_adx_status media_adx_container_fill(kosdec_adx_read_at read_at, void *context,
    uint8_t *buffer, unsigned *used, uint32_t *offset, unsigned bytes) {
    struct kosdec_adx_input_result in = read_at(context, *offset, buffer + *used, bytes);
    if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes)
        return KOSDEC_ADX_NEED_INPUT;
    if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > bytes)
        return in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
            KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
    *used += (unsigned)in.bytes;
    *offset += (uint32_t)in.bytes;
    return KOSDEC_ADX_MORE;
}

void media_adx_container_compact(uint8_t *buffer, unsigned *used, unsigned *position) {
    if(!*position)
        return;
    *used -= *position;
    if(*used)
        memmove(buffer, buffer + *position, *used);
    *position = 0;
}

enum kosdec_adx_status media_adx_container_stage(kosdec_adx_read_at read_at, void *context,
    uint8_t *buffer, unsigned capacity, unsigned *used, uint32_t *offset, uint32_t end) {
    for(unsigned calls = 0; calls < READ_CALL_LIMIT && *used < capacity && *offset < end; ++calls) {
        unsigned room = capacity - *used;
        if(room > MEDIA_ADX_CONTAINER_CHUNK)
            room = MEDIA_ADX_CONTAINER_CHUNK;
        unsigned request = media_adx_container_pcm_request(room, end - *offset);
        enum kosdec_adx_status status =
            media_adx_container_fill(read_at, context, buffer, used, offset, request);
        if(status != KOSDEC_ADX_MORE)
            return status;
    }
    return KOSDEC_ADX_MORE;
}
