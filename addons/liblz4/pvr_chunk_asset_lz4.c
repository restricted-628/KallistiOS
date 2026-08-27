/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/pvr_chunk_asset_lz4.h>

#include <lz4frame.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

int pvr_chunk_asset_lz4_decode(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes, void *dictionary_data) {
    const pvr_chunk_asset_lz4_dictionary_t *dictionary = dictionary_data;
    const uint8_t *source;
    uint8_t *output = destination;
    LZ4F_dctx *context = NULL;
    LZ4F_frameInfo_t info = LZ4F_INIT_FRAMEINFO;
    size_t source_offset = 0;
    size_t output_offset = 0;
    size_t input_bytes;
    size_t result;
    int saved_errno = EILSEQ;

    if(!section || !destination ||
       section->codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME ||
       destination_bytes != section->decoded_bytes) {
        errno = EINVAL;
        return -1;
    }
    if(section->dictionary_id &&
       (!dictionary || !dictionary->data || !dictionary->size ||
        dictionary->id != section->dictionary_id)) {
        errno = ENOENT;
        return -1;
    }

    source = section->stored_data;
    result = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);
    if(LZ4F_isError(result)) {
        errno = ENOMEM;
        return -1;
    }

    input_bytes = section->stored_bytes;
    result = LZ4F_getFrameInfo(context, &info, source, &input_bytes);
    if(LZ4F_isError(result) || info.frameType != LZ4F_frame ||
       (info.contentSize && info.contentSize != destination_bytes) ||
       info.dictID != section->dictionary_id)
        goto fail;
    source_offset = input_bytes;

    while(result != 0) {
        size_t source_bytes = section->stored_bytes - source_offset;
        size_t output_bytes = destination_bytes - output_offset;
        size_t previous_source = source_offset;
        size_t previous_output = output_offset;

        if(dictionary && section->dictionary_id) {
            result = LZ4F_decompress_usingDict(
                context, output + output_offset, &output_bytes,
                source + source_offset, &source_bytes, dictionary->data,
                dictionary->size, NULL);
        }
        else {
            result = LZ4F_decompress(
                context, output + output_offset, &output_bytes,
                source + source_offset, &source_bytes, NULL);
        }
        if(LZ4F_isError(result))
            goto fail;
        source_offset += source_bytes;
        output_offset += output_bytes;
        if(source_offset == previous_source && output_offset == previous_output)
            goto fail;
    }

    if(source_offset != section->stored_bytes ||
       output_offset != destination_bytes)
        goto fail;
    LZ4F_freeDecompressionContext(context);
    return 0;

fail:
    LZ4F_freeDecompressionContext(context);
    errno = saved_errno;
    return -1;
}
