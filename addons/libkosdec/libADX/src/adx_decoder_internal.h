#ifndef MEDIA_ADX_DECODER_INTERNAL_H
#define MEDIA_ADX_DECODER_INTERNAL_H
#include <kosdec/adx.h>
enum media_adx_header_mode {
    MEDIA_ADX_HEADER_STRICT,
    MEDIA_ADX_HEADER_PHASED_DIRECT,
    MEDIA_ADX_HEADER_MEMORY_JOB,
    MEDIA_ADX_HEADER_SECTOR_JOB
};
/* Initialized decoder, pending header, validated input span. */
struct kosdec_adx_result media_adx_decode_header(struct kosdec_adx_decoder *,
    const uint8_t *, size_t bytes, bool final_input, enum media_adx_header_mode);
#endif
