/* KallistiOS ##version##

   pvr_tilemap.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_tilemap.h>
#include <dc/pvr_frustum.h>
#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#define TILE_VERTICES (2u * PVR_FRUSTUM_CLIP_MAX_VERTICES)
#define TILE_FLAGS (PVR_CELL_FLIP_U | PVR_CELL_FLIP_V | PVR_CELL_HIDDEN)

typedef struct memory_range {
    uintptr_t start;
    size_t bytes;
} memory_range_t;

typedef struct tile_window {
    int64_t x0, y0, x1, y1;
    double a, b, c, d;
    size_t candidates;
    pvr_frustum_t clip;
    memory_range_t sources[5];
} tile_window_t;

static int fail(int error) {
    errno = error;
    return -1;
}

static int range_init(memory_range_t *range, const void *data, size_t count,
                      size_t size, size_t alignment) {
    uintptr_t address = (uintptr_t)data;

    if((count && !data) || address % alignment)
        return fail(EINVAL);
    if(count > SIZE_MAX / size || count * size > UINTPTR_MAX - address)
        return fail(ERANGE);
    range->start = address;
    range->bytes = count * size;
    return 0;
}

static int overlap(const memory_range_t *a, const memory_range_t *b) {
    return a->bytes && b->bytes && a->start < b->start + b->bytes &&
           b->start < a->start + a->bytes;
}

static int sources_overlap(const tile_window_t *window,
                           const memory_range_t *range) {
    for(size_t i = 0; i < 5; ++i)
        if(overlap(&window->sources[i], range))
            return 1;
    return 0;
}

static int address_valid(pvr_tilemap_address_t address) {
    return address >= PVR_TILEMAP_CLIP && address <= PVR_TILEMAP_CLAMP;
}

static int prepare(const pvr_tilemap_t *map, const pvr_tilemap_view_t *view,
                    tile_window_t *window, memory_range_t *view_range) {
    double min_x = DBL_MAX, max_x = -DBL_MAX;
    double min_y = DBL_MAX, max_y = -DBL_MAX;
    double sine, cosine, determinant;
    double x0, x1, y0, y1;
    uint64_t columns, rows;
    const matrix_t identity = {
        { 1, 0, 0, 0 }, { 0, 1, 0, 0 },
        { 0, 0, 1, 0 }, { 0, 0, 0, 1 }
    };

    if(!map || !view)
        return fail(EINVAL);
    if(range_init(&window->sources[0], map, 1, sizeof(*map),
                  alignof(pvr_tilemap_t)) < 0 ||
       range_init(view_range, view, 1, sizeof(*view),
                  alignof(pvr_tilemap_view_t)) < 0)
        return -1;
    if(!map->columns || !map->rows || map->columns > INT32_MAX ||
       map->rows > INT32_MAX || map->row_stride < map->columns ||
       !map->tile_count || !map->atlas.cell_count ||
       !isfinite(map->tile_width) || map->tile_width <= 0 ||
       !isfinite(map->tile_height) || map->tile_height <= 0 ||
       !address_valid(view->address_x) || !address_valid(view->address_y) ||
       !view->max_candidates || !isfinite(view->left) ||
       !isfinite(view->right) || !isfinite(view->top) ||
       !isfinite(view->bottom) || view->left >= view->right ||
       view->top >= view->bottom || !isfinite(view->anchor_x) ||
       !isfinite(view->anchor_y) || !isfinite(view->scroll_x) ||
       !isfinite(view->scroll_y) || !isfinite(view->rotation) ||
       !isfinite(view->scale_x) || view->scale_x <= 0 ||
       !isfinite(view->scale_y) || view->scale_y <= 0 ||
       !isfinite(view->depth) || view->depth <= FLT_MIN)
        return fail(EINVAL);
    if(map->rows - 1u > (SIZE_MAX - map->columns) / map->row_stride)
        return fail(ERANGE);
    if(map->index_count < (map->rows - 1u) * map->row_stride + map->columns)
        return fail(EINVAL);
    if(range_init(&window->sources[1], map->indices, map->index_count,
                  sizeof(*map->indices), alignof(uint32_t)) < 0 ||
       range_init(&window->sources[2], map->tiles, map->tile_count,
                  sizeof(*map->tiles), alignof(pvr_tilemap_tile_t)) < 0 ||
       range_init(&window->sources[3], map->atlas.cells, map->atlas.cell_count,
                  sizeof(*map->atlas.cells), alignof(pvr_sprite_cell_t)) < 0)
        return -1;
    window->sources[4] = *view_range;

#ifdef __DREAMCAST__
    shz_sincos_t angle = shz_sincosf(view->rotation);
    sine = angle.sin;
    cosine = angle.cos;
#else
    sine = sinf(view->rotation);
    cosine = cosf(view->rotation);
#endif
    window->a = cosine * view->scale_x;
    window->b = -sine * view->scale_y;
    window->c = sine * view->scale_x;
    window->d = cosine * view->scale_y;
    determinant = window->a * window->d - window->b * window->c;
    if(!isfinite(determinant) || determinant <= 0)
        return fail(ERANGE);

    /* Invert the same approximate rotation used for emission. Assuming
       sin^2+cos^2 is exactly one would invalidate conservative selection on
       the optimized target path. Double arithmetic keeps large scroll
       cancellation out of the float vertex pipeline. */
    for(size_t i = 0; i < 4; ++i) {
        double x = (double)(i & 1u ? view->right : view->left) - view->anchor_x;
        double y = (double)(i & 2u ? view->bottom : view->top) - view->anchor_y;
        double mx = (window->d * x - window->b * y) / determinant + view->scroll_x;
        double my = (window->a * y - window->c * x) / determinant + view->scroll_y;
        if(!isfinite(mx) || !isfinite(my))
            return fail(ERANGE);
        min_x = fmin(min_x, mx);
        max_x = fmax(max_x, mx);
        min_y = fmin(min_y, my);
        max_y = fmax(max_y, my);
    }
    x0 = floor(min_x / map->tile_width) - 1.0;
    x1 = ceil(max_x / map->tile_width) + 1.0;
    y0 = floor(min_y / map->tile_height) - 1.0;
    y1 = ceil(max_y / map->tile_height) + 1.0;
    if(!isfinite(x0) || !isfinite(x1) || !isfinite(y0) || !isfinite(y1) ||
       x0 < INT32_MIN || x1 > INT32_MAX || y0 < INT32_MIN || y1 > INT32_MAX)
        return fail(ERANGE);
    window->x0 = (int64_t)x0;
    window->x1 = (int64_t)x1;
    window->y0 = (int64_t)y0;
    window->y1 = (int64_t)y1;
    if(view->address_x == PVR_TILEMAP_CLIP) {
        if(window->x0 < 0)
            window->x0 = 0;
        if(window->x1 > (int64_t)map->columns)
            window->x1 = map->columns;
    }
    if(view->address_y == PVR_TILEMAP_CLIP) {
        if(window->y0 < 0)
            window->y0 = 0;
        if(window->y1 > (int64_t)map->rows)
            window->y1 = map->rows;
    }
    columns = window->x1 > window->x0 ? window->x1 - window->x0 : 0;
    rows = window->y1 > window->y0 ? window->y1 - window->y0 : 0;
    if(columns && rows > view->max_candidates / columns)
        return fail(E2BIG);
    window->candidates = (size_t)(columns * rows);
    if(!columns || !rows) {
        window->x1 = window->x0;
        window->y1 = window->y0;
    }
    return pvr_frustum_init(&window->clip, &identity, view->left, view->top,
                            view->right, view->bottom, 0.5f, 2.0f);
}

static size_t addressed(int64_t coordinate, size_t length,
                        pvr_tilemap_address_t policy) {
    if(policy == PVR_TILEMAP_WRAP) {
        coordinate %= (int64_t)length;
        if(coordinate < 0)
            coordinate += length;
    }
    else if(policy == PVR_TILEMAP_CLAMP) {
        if(coordinate < 0)
            coordinate = 0;
        if(coordinate >= (int64_t)length)
            coordinate = length - 1u;
    }
    return (size_t)coordinate;
}

static int tile_geometry(pvr_vertex_t output[TILE_VERTICES], size_t *count,
                          const pvr_tilemap_t *map,
                          const pvr_tilemap_view_t *view,
                          const tile_window_t *window,
                          const pvr_tilemap_tile_t *tile,
                          int64_t column, int64_t row) {
    alignas(32) pvr_vertex_t quad[4];
    pvr_cell_resolved_t cell = { 0 };
    pvr_sprite_cell_t uv;
    pvr_sprite_atlas_t atlas = { &uv, 1 };
    float min_x = FLT_MAX, max_x = -FLT_MAX;
    float min_y = FLT_MAX, max_y = -FLT_MAX;
    int inside = 1;

    *count = 0;
    if(tile->atlas_cell_index >= map->atlas.cell_count ||
       (tile->flags & ~TILE_FLAGS) ||
       (tile->list != PVR_LIST_OP_POLY && tile->list != PVR_LIST_TR_POLY &&
        tile->list != PVR_LIST_PT_POLY))
        return fail(EILSEQ);
    if(tile->flags & PVR_CELL_HIDDEN)
        return 0;
    uv = map->atlas.cells[tile->atlas_cell_index];
    uv.width = uv.height = 1.0f;
    uv.origin_x = uv.origin_y = 0.0f;
    cell.instance.position.z = view->depth;
    cell.instance.scale_x = cell.instance.scale_y = 1.0f;
    cell.instance.flags = tile->flags;
    memcpy(cell.argb, tile->argb, sizeof(cell.argb));
    memcpy(cell.oargb, tile->oargb, sizeof(cell.oargb));
    if(pvr_cell_sprite_compile_colored_2d(quad, 4, &atlas, &cell, 1, NULL) < 0)
        return fail(EILSEQ);
    for(size_t i = 0; i < 4; ++i) {
        double x = ((double)column + quad[i].x) * map->tile_width - view->scroll_x;
        double y = ((double)row + quad[i].y) * map->tile_height - view->scroll_y;
        double sx = view->anchor_x + window->a * x + window->b * y;
        double sy = view->anchor_y + window->c * x + window->d * y;
        if(!isfinite(sx) || !isfinite(sy) || fabs(sx) > FLT_MAX || fabs(sy) > FLT_MAX)
            return fail(ERANGE);
        quad[i].x = (float)sx;
        quad[i].y = (float)sy;
        min_x = fminf(min_x, quad[i].x);
        max_x = fmaxf(max_x, quad[i].x);
        min_y = fminf(min_y, quad[i].y);
        max_y = fmaxf(max_y, quad[i].y);
        inside &= quad[i].x >= view->left && quad[i].x <= view->right &&
                  quad[i].y >= view->top && quad[i].y <= view->bottom;
    }
    if(max_x <= view->left || min_x >= view->right ||
       max_y <= view->top || min_y >= view->bottom ||
       min_x == max_x || min_y == max_y)
        return 0;
    if(inside) {
        memcpy(output, quad, sizeof(quad));
        *count = 4;
        return 0;
    }

    /* Preserve the cell's A/B/D/C diagonal and interpolation by clipping
       each triangle separately. A screen-space identity frustum has W=1;
       restore the caller's constant PVR depth after its projection step. */
    const unsigned int corners[2][3] = { { 0, 1, 2 }, { 2, 1, 3 } };
    for(size_t triangle = 0; triangle < 2; ++triangle) {
        alignas(32) pvr_vertex_t input[3];
        pvr_frustum_clip_result_t clipped;
        for(size_t i = 0; i < 3; ++i) {
            input[i] = quad[corners[triangle][i]];
            input[i].flags = i == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
        }
        if(pvr_frustum_clip_triangle(output + *count, TILE_VERTICES - *count,
                                     input, &window->clip,
                                     PVR_FRUSTUM_CLIP_ALL, &clipped) < 0)
            return -1;
        /* Tangencies can leave a zero-area fan triangle at a viewport edge.
           Exclude those from both capacity counts and hardware submission. */
        size_t kept = 0;
        for(size_t i = 0; i < clipped.output_vertices; i += 3) {
            const pvr_vertex_t *t = output + *count + i;
            double area = ((double)t[1].x - t[0].x) * ((double)t[2].y - t[0].y) -
                          ((double)t[1].y - t[0].y) * ((double)t[2].x - t[0].x);
            if(area == 0.0)
                continue;
            memmove(output + *count + kept, t, 3u * sizeof(*output));
            for(size_t j = 0; j < 3; ++j)
                output[*count + kept + j].z = view->depth;
            kept += 3;
        }
        *count += kept;
    }
    return 0;
}

static int traverse(const pvr_tilemap_t *map, const pvr_tilemap_view_t *view,
                     const tile_window_t *window, pvr_vertex_t *vertices,
                     pvr_tilemap_draw_t *draws, pvr_tilemap_result_t *result) {
    alignas(32) pvr_vertex_t scratch[TILE_VERTICES];
    pvr_tilemap_result_t total = { .candidate_tiles = window->candidates };

    for(int64_t y = window->y0; y < window->y1; ++y) {
        size_t row = addressed(y, map->rows, view->address_y);
        for(int64_t x = window->x0; x < window->x1; ++x) {
            size_t column = addressed(x, map->columns, view->address_x);
            uint32_t index = map->indices[row * map->row_stride + column];
            size_t count;
            const pvr_tilemap_tile_t *tile;
            if(index == PVR_TILEMAP_EMPTY)
                continue;
            if(index >= map->tile_count)
                return fail(EILSEQ);
            tile = &map->tiles[index];
            if(tile_geometry(scratch, &count, map, view, window, tile, x, y) < 0)
                return -1;
            if(!count)
                continue;
            if(count > SIZE_MAX - total.vertex_count)
                return fail(ERANGE);
            if(vertices) {
                pvr_tilemap_draw_t draw = {
                    .first_vertex = total.vertex_count, .vertex_count = count,
                    .column = (int32_t)x, .row = (int32_t)y, .tile_index = index,
                    .material_id = tile->material_id, .priority = tile->priority,
                    .list = tile->list
                };
                memcpy(vertices + total.vertex_count, scratch,
                       count * sizeof(*vertices));
                draws[total.visible_tiles] = draw;
            }
            total.vertex_count += count;
            ++total.visible_tiles;
        }
    }
    *result = total;
    return 0;
}

int pvr_tilemap_measure(const pvr_tilemap_t *map,
                        const pvr_tilemap_view_t *view,
                        pvr_tilemap_result_t *result) {
    tile_window_t window;
    memory_range_t view_range, output_range;
    pvr_tilemap_result_t candidate;

    if(!result)
        return fail(EINVAL);
    if(prepare(map, view, &window, &view_range) < 0 ||
       range_init(&output_range, result, 1, sizeof(*result),
                  alignof(pvr_tilemap_result_t)) < 0)
        return -1;
    if(sources_overlap(&window, &output_range))
        return fail(EINVAL);
    if(traverse(map, view, &window, NULL, NULL, &candidate) < 0)
        return -1;
    *result = candidate;
    return 0;
}

int pvr_tilemap_compile(pvr_vertex_t *vertices, size_t vertex_capacity,
                        pvr_tilemap_draw_t *draws, size_t draw_capacity,
                        const pvr_tilemap_t *map,
                        const pvr_tilemap_view_t *view,
                        pvr_tilemap_result_t *result) {
    tile_window_t window;
    memory_range_t view_range, output[3];
    pvr_tilemap_result_t candidate;

    if(!result)
        return fail(EINVAL);
    if(prepare(map, view, &window, &view_range) < 0 ||
       range_init(&output[0], vertices, vertex_capacity, sizeof(*vertices), 32) < 0 ||
       range_init(&output[1], draws, draw_capacity, sizeof(*draws),
                  alignof(pvr_tilemap_draw_t)) < 0 ||
       range_init(&output[2], result, 1, sizeof(*result),
                  alignof(pvr_tilemap_result_t)) < 0)
        return -1;
    for(size_t i = 0; i < 3; ++i) {
        if(sources_overlap(&window, &output[i]))
            return fail(EINVAL);
        for(size_t j = 0; j < i; ++j)
            if(overlap(&output[i], &output[j]))
                return fail(EINVAL);
    }
    if(traverse(map, view, &window, NULL, NULL, &candidate) < 0)
        return -1;
    if(vertex_capacity < candidate.vertex_count ||
       draw_capacity < candidate.visible_tiles)
        return fail(ENOSPC);
    /* Sources are immutable for both traversals. No material callback or
       allocator can change them between preflight and publication. */
    if(traverse(map, view, &window, vertices, draws, &candidate) < 0)
        return -1;
    *result = candidate;
    return 0;
}
