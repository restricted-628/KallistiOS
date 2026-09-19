/* KallistiOS ##version##

   dc/pvr_cell_asset.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_cell_asset.h
    \brief   Pointer-free cell-sprite state and stream assets.
    \ingroup pvr_cell

    The format stores base cell state and timestamped stream keys without
    serializing pointers or host-sized fields. Checked assets materialize into
    the existing caller-owned cell runtime; no second sampler is introduced.
*/

#ifndef __DC_PVR_CELL_ASSET_H
#define __DC_PVR_CELL_ASSET_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

#include <dc/pvr_cell.h>

/** \addtogroup pvr_cell
    @{ */

/** \brief Little-endian bytes `PCA1` at the start of a cell asset. */
#define PVR_CELL_ASSET_MAGIC UINT32_C(0x31414350)

/** \brief Current cell-asset format version. */
#define PVR_CELL_ASSET_VERSION 1u

/** \brief Fixed serialized header size. */
#define PVR_CELL_ASSET_HEADER_BYTES 64u

/** \brief Fixed serialized complete-cell record size. */
#define PVR_CELL_ASSET_STATE_BYTES 72u

/** \brief Fixed serialized stream descriptor size. */
#define PVR_CELL_ASSET_STREAM_BYTES 20u

/** \brief Fixed serialized key record size. */
#define PVR_CELL_ASSET_KEY_BYTES 88u

/** \brief Pointer-free description of one serialized stream. */
typedef struct pvr_cell_asset_stream {
    uint32_t first_key;
    uint32_t key_count;
    float time_offset;
    float time_max;
    uint32_t repeat;
} pvr_cell_asset_stream_t;

/** \brief Checked immutable view of one serialized cell asset.

    The source byte image must remain immutable and accessible for the view's
    lifetime. Runtime objects produced by materialization borrow only the
    caller-owned decoded arrays, not this image.
*/
typedef struct pvr_cell_asset_view {
    const void *data;
    size_t size;
    const void *cells;
    size_t cell_count;
    const void *streams;
    size_t stream_count;
    const void *keys;
    size_t key_count;
} pvr_cell_asset_view_t;

/** \brief Materialized base state and ordered stream list. */
typedef struct pvr_cell_asset_runtime {
    const pvr_cell_state_t *base_cells;
    size_t cell_count;
    pvr_cell_stream_list_t stream_list;
} pvr_cell_asset_runtime_t;

/** \brief Validate source state and report its exact encoded size. */
int pvr_cell_asset_measure(const pvr_cell_state_t *base_cells,
                           size_t cell_count,
                           const pvr_cell_stream_t *streams,
                           size_t stream_count,
                           size_t *required_bytes);

/** \brief Serialize base state and streams into one pointer-free asset.

    Validation completes before output is modified. Output may not overlap the
    base state, stream descriptors, or any key array. On success,
    \p written_bytes receives the exact byte count when non-NULL.
*/
int pvr_cell_asset_encode(const pvr_cell_state_t *base_cells,
                          size_t cell_count,
                          const pvr_cell_stream_t *streams,
                          size_t stream_count,
                          void *output, size_t output_size,
                          size_t *written_bytes);

/** \brief Parse and completely validate one serialized cell asset. */
int pvr_cell_asset_open(const void *data, size_t size,
                        pvr_cell_asset_view_t *view);

/** \brief Decode one admitted base cell by index. */
int pvr_cell_asset_state_get(const pvr_cell_asset_view_t *view, size_t index,
                             pvr_cell_state_t *state);

/** \brief Decode one admitted stream descriptor by index. */
int pvr_cell_asset_stream_get(const pvr_cell_asset_view_t *view, size_t index,
                              pvr_cell_asset_stream_t *stream);

/** \brief Decode one admitted stream key by global key index. */
int pvr_cell_asset_key_get(const pvr_cell_asset_view_t *view, size_t index,
                           pvr_cell_key_t *key);

/** \brief Materialize an admitted asset into the cell runtime.

    Every capacity, alignment, and overlap is checked before output storage is
    modified. Stream views borrow the caller-owned key array and the returned
    stream list borrows the caller-owned view array. The runtime descriptor is
    published last and remains unchanged on failure.
*/
int pvr_cell_asset_materialize(
    const pvr_cell_asset_view_t *view,
    pvr_cell_state_t *cells, size_t cell_capacity,
    pvr_cell_key_t *keys, size_t key_capacity,
    pvr_cell_stream_view_t *streams, size_t stream_capacity,
    pvr_cell_asset_runtime_t *runtime);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CELL_ASSET_H */
