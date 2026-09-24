/* KallistiOS ##version##

   kos/pvr_chunk_asset_lz4.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    kos/pvr_chunk_asset_lz4.h
    \brief   LZ4 Frame decoder for compact-model asset sections.
*/

#ifndef __KOS_PVR_CHUNK_ASSET_LZ4_H
#define __KOS_PVR_CHUNK_ASSET_LZ4_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_chunk_asset.h>

/** \addtogroup pvr_chunk_model
    @{
*/

/** \brief Optional dictionary supplied to the LZ4 Frame decoder. */
typedef struct pvr_chunk_asset_lz4_dictionary {
    const void *data;
    size_t size;
    uint32_t id;
} pvr_chunk_asset_lz4_dictionary_t;

/** \brief Header-derived requirements for one resident frame.

    Scratch bytes are the two upstream 1.10.0 buffer requests, not total heap
    use: context, wrapper, job, allocator overhead, and service storage are
    excluded. Source, output, and dictionary storage are caller-owned. The
    dictionary count is zero when the frame does not use a dictionary.
*/
typedef struct pvr_chunk_asset_lz4_requirements {
    size_t stored_bytes;
    size_t decoded_bytes;
    size_t dictionary_bytes;
    size_t block_bytes;
    size_t scratch_bytes;
    int independent_blocks;
} pvr_chunk_asset_lz4_requirements_t;

/** \brief Optional per-decoder admission limits, not a global RAM budget.

    Zero byte limits mean unlimited. Any nonzero require_independent_blocks
    rejects linked frames. Limits are checked before allocating frame scratch;
    small wrapper/context allocations may precede the check.
*/
typedef struct pvr_chunk_asset_lz4_limits {
    size_t max_block_bytes;
    size_t max_scratch_bytes;
    int require_independent_blocks;
} pvr_chunk_asset_lz4_limits_t;

/** \brief Initializer for the converter's 64 KiB independent-block profile. */
#define PVR_CHUNK_ASSET_LZ4_COMPACT_LIMITS {65536, 131076, 1}

/** \brief Inspect a frame header without allocating scratch or decoding data.

    Allocates and frees a temporary upstream context. Validates the header,
    declared content size, and dictionary ID, but not payload or checksums of
    payload data. Requirements are unchanged on error. ENOMEM reports context
    allocation failure; EILSEQ reports an invalid/inconsistent frame header.
    EINVAL reports invalid arguments; ENOENT reports a missing/mismatched
    caller dictionary when the section requires one.
*/
int pvr_chunk_asset_lz4_get_requirements(
    const pvr_chunk_asset_section_t *section,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    pvr_chunk_asset_lz4_requirements_t *requirements);

/** \brief Opaque incremental LZ4 Frame decode state. */
typedef struct pvr_chunk_asset_lz4_state pvr_chunk_asset_lz4_state_t;

/** \brief Coherent progress for one incremental section decode. */
typedef struct pvr_chunk_asset_lz4_progress {
    size_t source_bytes;
    size_t source_total;
    size_t output_bytes;
    size_t output_total;
    int complete;
} pvr_chunk_asset_lz4_progress_t;

/** \brief Incremental decoder needs more calls. */
#define PVR_CHUNK_ASSET_LZ4_MORE 0

/** \brief Incremental decoder completed and verified the section. */
#define PVR_CHUNK_ASSET_LZ4_COMPLETE 1

/** \brief Create incremental state for one complete resident LZ4 Frame.

    Source, destination, and optional dictionary storage are borrowed until
    pvr_chunk_asset_lz4_state_destroy(). Source and destination must not
    overlap. Creating state allocates the wrapper, upstream LZ4 Frame context,
    and all frame scratch buffers before returning; ENOMEM is reported here.
    No payload is consumed or output written during creation. Subsequent steps
    allocate nothing. Scratch depends on the frame's advertised block size:
    upstream 1.10.0 requests 2*block_size+4 bytes for independent blocks, plus
    128 KiB for linked blocks, excluding context/allocator overhead. This API
    retains support for all upstream frame block sizes; creation is not a
    memory-budget admission policy. Use state_create_with_limits() to opt in
    to a per-decoder block-size/scratch policy.
*/
pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary);

/** \brief Create state with explicit frame-profile and scratch limits.

    Same ownership/stepping contract as state_create(). NULL limits preserve
    its unrestricted policy. EFBIG reports a valid header exceeding a limit;
    rejection does not consume payload or modify destination. Each concurrent
    state or queued job owns separate scratch: these are not aggregate limits.
*/
pvr_chunk_asset_lz4_state_t *pvr_chunk_asset_lz4_state_create_with_limits(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes,
    const pvr_chunk_asset_lz4_dictionary_t *dictionary,
    const pvr_chunk_asset_lz4_limits_t *limits);

/** \brief Decode up to one caller-selected output budget.

    A positive output budget bounds bytes published during this call. LZ4 may
    internally decode a whole frame block while satisfying a small output
    buffer, and a large budget may process multiple blocks. This is not a CPU
    time or input-work budget. The converter emits 64 KiB independent blocks.

    \retval PVR_CHUNK_ASSET_LZ4_MORE More calls are required.
    \retval PVR_CHUNK_ASSET_LZ4_COMPLETE Output size and CRC are verified.
    \retval -1 Decode failed, with errno set.
*/
int pvr_chunk_asset_lz4_state_step(pvr_chunk_asset_lz4_state_t *state,
                                   size_t output_budget);

/** \brief Copy progress from an incremental decoder. */
int pvr_chunk_asset_lz4_state_get_progress(
    const pvr_chunk_asset_lz4_state_t *state,
    pvr_chunk_asset_lz4_progress_t *progress);

/** \brief Destroy incremental state without touching borrowed storage. */
void pvr_chunk_asset_lz4_state_destroy(
    pvr_chunk_asset_lz4_state_t *state);

/** \brief Decode exactly one LZ4 Frame compact-model section.

    Pass this function directly as pvr_chunk_asset_decoder_t. The callback
    context may be NULL for a frame without a dictionary, or point to one
    pvr_chunk_asset_lz4_dictionary_t. Frame checksums remain enabled.
*/
int pvr_chunk_asset_lz4_decode(
    const pvr_chunk_asset_section_t *section, void *destination,
    size_t destination_bytes, void *dictionary);

/** @} */

__END_DECLS

#endif /* __KOS_PVR_CHUNK_ASSET_LZ4_H */
