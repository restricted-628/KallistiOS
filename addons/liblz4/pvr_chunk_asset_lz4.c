/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos/pvr_chunk_asset_lz4.h>

/* Error classification is a static API of the pinned LZ4 release. */
#define LZ4F_STATIC_LINKING_ONLY
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

static int frame_errno(size_t result) {
    return LZ4F_getErrorCode(result) == LZ4F_ERROR_allocation_failed ?
           ENOMEM : EILSEQ;
}

static int validate_section(const pvr_chunk_asset_section_t *section,
                           const pvr_chunk_asset_lz4_dictionary_t *dictionary) {
    if(!section || !section->stored_data || !section->stored_bytes ||
       !section->decoded_bytes ||
       section->codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME) {
        errno = EINVAL;
        return -1;
    }
    if(section->dictionary_id &&
       (!dictionary || !dictionary->data || !dictionary->size ||
        dictionary->id != section->dictionary_id)) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

/* Keep sizing tied to the pinned upstream dstage_init implementation. Never
   infer an opaque context size; the public estimate covers scratch only. */
static int read_header(LZ4F_dctx *context,
                       const pvr_chunk_asset_section_t *section,
                       const pvr_chunk_asset_lz4_dictionary_t *dictionary,
                       pvr_chunk_asset_lz4_requirements_t *requirements,
                       size_t *header_bytes) {
    LZ4F_frameInfo_t info = LZ4F_INIT_FRAMEINFO;
    size_t input_bytes = section->stored_bytes;
    size_t block_bytes;
    size_t result = LZ4F_getFrameInfo(context, &info, section->stored_data,
                                     &input_bytes);

    if(LZ4F_isError(result) || info.frameType != LZ4F_frame ||
       (info.contentSize && info.contentSize != section->decoded_bytes) ||
       info.dictID != section->dictionary_id || !result) {
        errno = LZ4F_isError(result) ? frame_errno(result) : EILSEQ;
        return -1;
    }
    switch(info.blockSizeID) {
        case LZ4F_max64KB: block_bytes = 64u * 1024u; break;
        case LZ4F_max256KB: block_bytes = 256u * 1024u; break;
        case LZ4F_max1MB: block_bytes = 1024u * 1024u; break;
        case LZ4F_max4MB: block_bytes = 4u * 1024u * 1024u; break;
        default:
            errno = EILSEQ;
            return -1;
    }
    *requirements = (pvr_chunk_asset_lz4_requirements_t) {
        .stored_bytes = section->stored_bytes,
        .decoded_bytes = section->decoded_bytes,
        .dictionary_bytes = section->dictionary_id ? dictionary->size : 0,
        .block_bytes = block_bytes,
        .scratch_bytes = 2 * block_bytes + 4 +
                         (info.blockMode == LZ4F_blockLinked ? 131072u : 0),
        .independent_blocks = info.blockMode == LZ4F_blockIndependent
    };
    *header_bytes = input_bytes;
    return 0;
}

int pvr_chunk_asset_lz4_get_requirements(
    const pvr_chunk_asset_section_t *section,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    pvr_chunk_asset_lz4_requirements_t *requirements) {
    LZ4F_dctx *context;
    size_t result;
    size_t header_bytes;
    int status;
    int error;

    if(!requirements) {
        errno = EINVAL;
        return -1;
    }
    if(validate_section(section, dictionary) < 0)
        return -1;
    result = LZ4F_createDecompressionContext(&context, LZ4F_VERSION);
    if(LZ4F_isError(result)) {
        errno = frame_errno(result);
        return -1;
    }
    status = read_header(context, section, dictionary, requirements,
                         &header_bytes);
    error = errno;
    LZ4F_freeDecompressionContext(context);
    if(status < 0)
        errno = error;
    return status;
}

pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create_with_limits(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    const pvr_chunk_asset_lz4_limits_t *limits) {
    pvr_chunk_asset_lz4_state_t *state;
    pvr_chunk_asset_lz4_requirements_t requirements;
    size_t result;
    int overlap;

    if(validate_section(section, dictionary) < 0)
        return NULL;
    if(!destination || destination_bytes != section->decoded_bytes) {
        errno = EINVAL;
        return NULL;
    }
    overlap = ranges_overlap(section->stored_data, section->stored_bytes,
                             destination, destination_bytes);
    if(overlap) {
        errno = overlap < 0 ? EOVERFLOW : EINVAL;
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
        errno = frame_errno(result);
        return NULL;
    }

    if(read_header(state->context, section, dictionary, &requirements,
                    &state->source_offset) < 0) {
        int error = errno;

        LZ4F_freeDecompressionContext(state->context);
        free(state);
        errno = error;
        return NULL;
    }
    if(limits &&
       ((limits->max_block_bytes &&
         requirements.block_bytes > limits->max_block_bytes) ||
        (limits->max_scratch_bytes &&
         requirements.scratch_bytes > limits->max_scratch_bytes) ||
        (limits->require_independent_blocks && !requirements.independent_blocks))) {
        LZ4F_freeDecompressionContext(state->context);
        free(state);
        errno = EFBIG;
        return NULL;
    }

    /* getFrameInfo() admits the header but leaves allocation to dstage_init.
       Prime that stage without consuming payload or publishing output, so a
       successfully created state cannot first discover ENOMEM on its fiber.
       Keep non-NULL pointers even for these zero-sized buffers. */
    {
        size_t source_bytes = 0;
        size_t output_bytes = 0;

        /* Install the dictionary before leaving dstage_init: upstream only
           accepts a new dictionary through that stage, not on later calls. */
        result = LZ4F_decompress_usingDict(
            state->context, destination, &output_bytes,
            (const uint8_t *)section->stored_data + state->source_offset,
            &source_bytes,
            section->dictionary_id ? dictionary->data : NULL,
            section->dictionary_id ? dictionary->size : 0, NULL);
        if(LZ4F_isError(result)) {
            int error = frame_errno(result);

            LZ4F_freeDecompressionContext(state->context);
            free(state);
            errno = error;
            return NULL;
        }
    }
    state->hint = result;
    return state;
}

pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary) {
    return pvr_chunk_asset_lz4_state_create_with_limits(
        section, destination, destination_bytes, dictionary, NULL);
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
            return state_fail(state, frame_errno(result));

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
