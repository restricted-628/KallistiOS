/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/pvr_chunk_asset_lz4.h>

#include <lz4frame.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct pvr_chunk_asset_lz4_state {
    pvr_chunk_asset_section_t section;
    const pvr_chunk_asset_lz4_dictionary_t *dictionary;
    uint8_t *destination;
    LZ4F_dctx *context;
    size_t source_offset;
    size_t output_offset;
    size_t hint;
    uint32_t crc;
    int error;
    bool complete;
};

static uint32_t crc32_update(uint32_t crc, const void *data, size_t size) {
    const uint8_t *bytes = data;
    size_t index;

    for(index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}

static int ranges_overlap(const void *left, size_t left_size,
                          const void *right, size_t right_size) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if(left_start > UINTPTR_MAX - left_size ||
       right_start > UINTPTR_MAX - right_size)
        return -1;
    return left_start < right_start + right_size &&
           right_start < left_start + left_size;
}

static int state_fail(pvr_chunk_asset_lz4_state_t *state, int error) {
    state->error = error;
    errno = error;
    return -1;
}

pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary) {
    pvr_chunk_asset_lz4_state_t *state;
    LZ4F_frameInfo_t info = LZ4F_INIT_FRAMEINFO;
    size_t input_bytes;
    size_t result;
    int overlap;

    if(!section || !section->stored_data || !section->stored_bytes ||
       !section->decoded_bytes || !destination ||
       section->codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME ||
       destination_bytes != section->decoded_bytes) {
        errno = EINVAL;
        return NULL;
    }
    overlap = ranges_overlap(section->stored_data, section->stored_bytes,
                             destination, destination_bytes);
    if(overlap) {
        errno = overlap < 0 ? EOVERFLOW : EINVAL;
        return NULL;
    }
    if(section->dictionary_id &&
       (!dictionary || !dictionary->data || !dictionary->size ||
        dictionary->id != section->dictionary_id)) {
        errno = ENOENT;
        return NULL;
    }

    state = calloc(1, sizeof(*state));
    if(!state) {
        errno = ENOMEM;
        return NULL;
    }
    state->section = *section;
    state->destination = destination;
    state->dictionary = dictionary;
    state->crc = UINT32_MAX;
    result = LZ4F_createDecompressionContext(&state->context, LZ4F_VERSION);
    if(LZ4F_isError(result)) {
        free(state);
        errno = ENOMEM;
        return NULL;
    }

    input_bytes = section->stored_bytes;
    result = LZ4F_getFrameInfo(state->context, &info, section->stored_data,
                               &input_bytes);
    if(LZ4F_isError(result) || info.frameType != LZ4F_frame ||
       (info.contentSize && info.contentSize != destination_bytes) ||
       info.dictID != section->dictionary_id || !result) {
        LZ4F_freeDecompressionContext(state->context);
        free(state);
        errno = EILSEQ;
        return NULL;
    }
    state->source_offset = input_bytes;
    state->hint = result;
    return state;
}

int pvr_chunk_asset_lz4_state_step(pvr_chunk_asset_lz4_state_t *state,
                                   size_t output_budget) {
    const uint8_t *source;
    size_t step_output = 0;

    if(!state || !output_budget) {
        errno = EINVAL;
        return -1;
    }
    if(state->error) {
        errno = state->error;
        return -1;
    }
    if(state->complete)
        return PVR_CHUNK_ASSET_LZ4_COMPLETE;

    source = state->section.stored_data;
    while(step_output < output_budget && state->hint) {
        size_t source_bytes = state->section.stored_bytes -
                              state->source_offset;
        size_t output_bytes = state->section.decoded_bytes -
                              state->output_offset;
        size_t budget_left = output_budget - step_output;
        size_t previous_source = state->source_offset;
        size_t previous_output = state->output_offset;
        size_t result;

        if(output_bytes > budget_left)
            output_bytes = budget_left;
        if(state->dictionary && state->section.dictionary_id) {
            result = LZ4F_decompress_usingDict(
                state->context, state->destination + state->output_offset,
                &output_bytes, source + state->source_offset, &source_bytes,
                state->dictionary->data, state->dictionary->size, NULL);
        }
        else {
            result = LZ4F_decompress(
                state->context, state->destination + state->output_offset,
                &output_bytes, source + state->source_offset, &source_bytes,
                NULL);
        }
        if(LZ4F_isError(result))
            return state_fail(state, EILSEQ);

        state->crc = crc32_update(state->crc,
                                  state->destination + state->output_offset,
                                  output_bytes);
        state->source_offset += source_bytes;
        state->output_offset += output_bytes;
        step_output += output_bytes;
        state->hint = result;
        if(state->source_offset == previous_source &&
           state->output_offset == previous_output)
            return state_fail(state, EILSEQ);
    }

    if(state->hint)
        return PVR_CHUNK_ASSET_LZ4_MORE;
    if(state->source_offset != state->section.stored_bytes ||
       state->output_offset != state->section.decoded_bytes ||
       ~state->crc != state->section.decoded_crc32)
        return state_fail(state, EILSEQ);

    state->complete = true;
    return PVR_CHUNK_ASSET_LZ4_COMPLETE;
}

int pvr_chunk_asset_lz4_state_get_progress(
    const pvr_chunk_asset_lz4_state_t *state,
    pvr_chunk_asset_lz4_progress_t *progress) {
    if(progress)
        memset(progress, 0, sizeof(*progress));
    if(!state || !progress) {
        errno = EINVAL;
        return -1;
    }

    progress->source_bytes = state->source_offset;
    progress->source_total = state->section.stored_bytes;
    progress->output_bytes = state->output_offset;
    progress->output_total = state->section.decoded_bytes;
    progress->complete = state->complete;
    return 0;
}

void pvr_chunk_asset_lz4_state_destroy(
    pvr_chunk_asset_lz4_state_t *state) {
    if(!state)
        return;
    LZ4F_freeDecompressionContext(state->context);
    free(state);
}

int pvr_chunk_asset_lz4_decode(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes, void *dictionary_data) {
    pvr_chunk_asset_lz4_state_t *state;
    int result;
    int saved_errno;

    state = pvr_chunk_asset_lz4_state_create(section, destination,
                                             destination_bytes,
                                             dictionary_data);
    if(!state)
        return -1;
    do {
        result = pvr_chunk_asset_lz4_state_step(state, destination_bytes);
    } while(result == PVR_CHUNK_ASSET_LZ4_MORE);
    saved_errno = errno;
    pvr_chunk_asset_lz4_state_destroy(state);
    if(result != PVR_CHUNK_ASSET_LZ4_COMPLETE) {
        errno = result < 0 ? saved_errno : EILSEQ;
        return -1;
    }
    return 0;
}
