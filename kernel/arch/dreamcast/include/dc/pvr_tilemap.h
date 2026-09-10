/* KallistiOS ##version##

   dc/pvr_tilemap.h
   Copyright (C) 2026 Joseph Black
*/

/** \file dc/pvr_tilemap.h
    \brief Bounded scrolling maps over existing cell geometry.
    \ingroup pvr_tilemap
*/

#ifndef __DC_PVR_TILEMAP_H
#define __DC_PVR_TILEMAP_H

#include <kos/cdefs.h>
#include <dc/pvr_cell.h>

__BEGIN_DECLS

/** \defgroup pvr_tilemap Scrolling tile maps
    \brief Caller-owned map selection, transforms, and viewport clipping
    \ingroup pvr_cell
    @{ */

/** Empty map index; no tile definition or atlas entry is accessed. */
#define PVR_TILEMAP_EMPTY UINT32_MAX

/** \brief Independent addressing policy for each map axis. */
typedef enum pvr_tilemap_address {
    PVR_TILEMAP_CLIP = 0, /**< Outside the finite map is empty. */
    PVR_TILEMAP_WRAP,     /**< Repeat the map, including negative coordinates. */
    PVR_TILEMAP_CLAMP     /**< Repeat the nearest edge tile outside the map. */
} pvr_tilemap_address_t;

/** \brief Reusable tile appearance and application routing.

    Flags use PVR_CELL_FLIP_U/V and PVR_CELL_HIDDEN. Colors use the existing
    cell A/B/C/D rectangle order. List must be an opaque, punch-through, or
    translucent polygon list. Priority is metadata; it does not change depth.
*/
typedef struct pvr_tilemap_tile {
    size_t atlas_cell_index;
    uint32_t flags;
    uint32_t material_id;
    int32_t priority;
    pvr_list_t list;
    uint32_t argb[4];
    uint32_t oargb[4];
} pvr_tilemap_tile_t;

/** \brief Read-only map storage and tile definitions.

    Row stride is in uint32_t indices and may exceed columns. index_count
    bounds the complete backing array. Atlas cells supply only their UV
    rectangle: their dimensions and pivots are replaced by the map's positive
    tile_width/height. Only candidate indices and referenced definitions/UVs
    are inspected, so cost does not scale with off-screen map storage.
*/
typedef struct pvr_tilemap {
    const uint32_t *indices;
    size_t index_count;
    size_t columns;
    size_t rows;
    size_t row_stride;
    const pvr_tilemap_tile_t *tiles;
    size_t tile_count;
    pvr_sprite_atlas_t atlas;
    float tile_width;
    float tile_height;
} pvr_tilemap_t;

/** \brief Screen viewport and map-to-screen transform.

    screen = anchor + rotation(scale * (map_position - scroll)). Rotation
    is in radians; scales must be positive. Coordinates and depth must be
    finite, and depth uses positive PVR reciprocal-W units. left/top/right/
    bottom define an independent, nonempty screen rectangle.

    max_candidates is a required nonzero work limit. Inverse viewport bounds
    include a one-tile rounding margin; E2BIG rejects an over-budget window
    before any map index is read. Signed logical tile coordinates must fit
    int32_t, including that margin. Excessive transforms return ERANGE.
*/
typedef struct pvr_tilemap_view {
    float left, top, right, bottom;
    float anchor_x, anchor_y;
    float scroll_x, scroll_y;
    float rotation;
    float scale_x, scale_y;
    float depth;
    pvr_tilemap_address_t address_x, address_y;
    size_t max_candidates;
} pvr_tilemap_view_t;

/** \brief One visible logical tile's geometry and routing metadata.

    Interior tiles use four-vertex strips. Clipped tiles use independent
    three-vertex triangles. All strips terminate with PVR_CMD_VERTEX_EOL and
    use pvr_vertex_t, suitable for existing canonical geometry sinks and
    polygon materials. Sorting these descriptors never moves vertex storage.
*/
typedef struct pvr_tilemap_draw {
    size_t first_vertex;
    size_t vertex_count;
    int32_t column, row; /**< Logical coordinates, before wrap/clamp. */
    uint32_t tile_index;
    uint32_t material_id;
    int32_t priority;
    pvr_list_t list;
} pvr_tilemap_draw_t;

/** \brief Exact counts for a stable map/view pair. */
typedef struct pvr_tilemap_result {
    size_t candidate_tiles;
    size_t visible_tiles; /**< Required draw descriptors. */
    size_t vertex_count;
} pvr_tilemap_result_t;

/** \brief Validate a visible region and measure its exact output capacity.

    No output geometry is written. Result is unchanged on failure. Sources
    must remain stable for the call; only candidate references are validated.
    Errors: EINVAL for descriptors, EILSEQ for invalid referenced tile data,
    ERANGE for arithmetic/range overflow, E2BIG for the candidate budget.
*/
int pvr_tilemap_measure(const pvr_tilemap_t *map,
                        const pvr_tilemap_view_t *view,
                        pvr_tilemap_result_t *result);

/** \brief Compile the visible map into caller-owned geometry and draw records.

    A complete preflight precedes publication. Insufficient capacity returns
    ENOSPC without changing any output. The map, view, atlas, definitions and
    index data must remain stable until return. Output buffers and result
    must not overlap each other or any source. Vertex storage is 32-byte
    aligned; NULL buffers are accepted only with zero corresponding capacity.
    Draw records are produced in logical row-major order. The application
    chooses material submission, priority ordering and list routing.

    Uses fixed per-call stack scratch, no heap, cache, worker or persistent
    state. Trigonometry uses SH4ZAM on Dreamcast; edge clipping and colored
    cell expansion reuse the existing graphics stack.
*/
int pvr_tilemap_compile(pvr_vertex_t *vertices, size_t vertex_capacity,
                        pvr_tilemap_draw_t *draws, size_t draw_capacity,
                        const pvr_tilemap_t *map,
                        const pvr_tilemap_view_t *view,
                        pvr_tilemap_result_t *result);

/** @} */
__END_DECLS
#endif /* __DC_PVR_TILEMAP_H */
