/* KallistiOS ##version##

   Coverage and admission tests for bounded scrolling maps.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_tilemap.h>
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

#ifdef __DREAMCAST__
#include <dc/matrix.h>
#endif

#ifndef __DREAMCAST__
int pvr_prim(const void *data, size_t size) {
    (void)data; (void)size; return 0;
}
int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list; (void)data; (void)size; return 0;
}
#endif

static pvr_sprite_cell_t cells[2] = {
    { 16, 16, 0.5f, 0.5f, 0, 0, 0.5f, 1 },
    { 32, 8, 0, 0, 0.5f, 0, 1, 1 }
};
static pvr_tilemap_tile_t tiles[3];
static uint32_t indices[8] = { 0, 1, 0, 99, 1, 0, 1, 99 };
alignas(32) static pvr_vertex_t vertices[8192], saved_vertices[8192];
static pvr_tilemap_draw_t draws[512], saved_draws[512];

static pvr_tilemap_t base_map(void) {
    for(size_t i = 0; i < 3; ++i) {
        tiles[i] = (pvr_tilemap_tile_t) {
            .atlas_cell_index = i % 2, .material_id = (uint32_t)i + 10,
            .priority = (int32_t)i - 1,
            .list = i == 0 ? PVR_LIST_OP_POLY :
                    i == 1 ? PVR_LIST_PT_POLY : PVR_LIST_TR_POLY
        };
        for(size_t j = 0; j < 4; ++j)
            tiles[i].argb[j] = UINT32_C(0xff4080c0);
    }
    return (pvr_tilemap_t) {
        .indices = indices, .index_count = 8, .columns = 3, .rows = 2,
        .row_stride = 4, .tiles = tiles, .tile_count = 3,
        .atlas = { cells, 2 }, .tile_width = 4, .tile_height = 4
    };
}

static pvr_tilemap_view_t base_view(void) {
    return (pvr_tilemap_view_t) {
        .left = 0, .top = 0, .right = 12, .bottom = 8,
        .scale_x = 1, .scale_y = 1, .depth = 0.25f,
        .max_candidates = 512
    };
}

static double edge(const pvr_vertex_t *a, const pvr_vertex_t *b,
                    double x, double y) {
    return (x - a->x) * ((double)b->y - a->y) -
           (y - a->y) * ((double)b->x - a->x);
}

static int covers(const pvr_vertex_t *a, const pvr_vertex_t *b,
                   const pvr_vertex_t *c, double x, double y) {
    double ab = edge(a, b, x, y), bc = edge(b, c, x, y), ca = edge(c, a, x, y);
    if(edge(a, b, c->x, c->y) == 0.0) return 0;
    return (ab > 0 && bc > 0 && ca > 0) || (ab < 0 && bc < 0 && ca < 0);
}

/* Independent software raster coverage catches wrong strip diagonals,
   duplicate clipped triangles, missing edge tiles, and rotated selection.
   Unequal noncentral sample offsets avoid exact diagonal/edge ambiguity. */
static void coverage(const pvr_tilemap_t *map, const pvr_tilemap_view_t *view,
                      const pvr_tilemap_result_t *result) {
    const double sine = sinf(view->rotation), cosine = cosf(view->rotation);
    for(unsigned y = 0; y < 17; ++y) {
        for(unsigned x = 0; x < 23; ++x) {
            double sx = view->left + (view->right - view->left) * (x + 0.317) / 23;
            double sy = view->top + (view->bottom - view->top) * (y + 0.619) / 17;
            double dx = sx - view->anchor_x, dy = sy - view->anchor_y;
            double norm = sine * sine + cosine * cosine;
            double mx = (cosine * dx + sine * dy) / (view->scale_x * norm) +
                        view->scroll_x;
            double my = (-sine * dx + cosine * dy) / (view->scale_y * norm) +
                        view->scroll_y;
            unsigned expected =
                (view->address_x != PVR_TILEMAP_CLIP ||
                 (mx >= 0 && mx < map->columns * map->tile_width)) &&
                (view->address_y != PVR_TILEMAP_CLIP ||
                 (my >= 0 && my < map->rows * map->tile_height));
            unsigned count = 0;
            for(size_t d = 0; d < result->visible_tiles; ++d) {
                const pvr_tilemap_draw_t *draw = &draws[d];
                const pvr_vertex_t *v = vertices + draw->first_vertex;
                for(size_t i = 2; i < draw->vertex_count; ++i) {
                    if(v[i - 1].flags == PVR_CMD_VERTEX_EOL ||
                       v[i - 2].flags == PVR_CMD_VERTEX_EOL) continue;
                    count += covers(v + i - 2, v + i - 1, v + i, sx, sy);
                }
            }
            assert(count == expected);
        }
    }
}

static pvr_tilemap_result_t compile(const pvr_tilemap_t *map,
                                   const pvr_tilemap_view_t *view) {
    pvr_tilemap_result_t measured, result;
#ifdef __DREAMCAST__
    const matrix_t seed = {
        { 1, 2, 3, 4 }, { 5, 6, 7, 8 },
        { 9, 10, 11, 12 }, { 13, 14, 15, 16 }
    };
    matrix_t saved, observed;
    mat_store(&saved);
    mat_load(&seed);
#endif
    assert(pvr_tilemap_measure(map, view, &measured) == 0);
    assert(measured.vertex_count <= 8192 && measured.visible_tiles <= 512);
    assert(pvr_tilemap_compile(vertices, measured.vertex_count, draws,
                              measured.visible_tiles, map, view, &result) == 0);
#ifdef __DREAMCAST__
    mat_store(&observed);
    mat_load(&saved);
    assert(!memcmp(&observed, &seed, sizeof(seed)));
#endif
    assert(result.candidate_tiles == measured.candidate_tiles);
    assert(result.visible_tiles == measured.visible_tiles);
    assert(result.vertex_count == measured.vertex_count);
    for(size_t i = 0; i < result.vertex_count; ++i) {
        assert(isfinite(vertices[i].x) && isfinite(vertices[i].y));
        assert(vertices[i].x >= view->left - 0.0001f);
        assert(vertices[i].x <= view->right + 0.0001f);
        assert(vertices[i].y >= view->top - 0.0001f);
        assert(vertices[i].y <= view->bottom + 0.0001f);
        assert(vertices[i].z == view->depth);
    }
    return result;
}

static void test_maps(void) {
    pvr_tilemap_t map = base_map();
    pvr_tilemap_view_t view = base_view();
    pvr_tilemap_result_t result = compile(&map, &view);
    assert(result.visible_tiles == 6 && result.vertex_count == 24);
    assert(draws[1].list == PVR_LIST_PT_POLY && draws[1].material_id == 11);
    assert(draws[0].priority == -1);
    assert(vertices[0].x == 0 && vertices[0].y == 4);
    coverage(&map, &view, &result);

    for(int flags = 0; flags < 4; ++flags) {
        tiles[0].flags = flags;
        result = compile(&map, &view);
        assert(vertices[0].u == (flags & PVR_CELL_FLIP_U ? 0.5f : 0));
        assert(vertices[0].v == (flags & PVR_CELL_FLIP_V ? 0 : 1));
        coverage(&map, &view, &result);
    }
    tiles[0].flags = 0;
    view.scroll_x = -3.375f;
    view.scroll_y = 2.625f;
    for(int policy = PVR_TILEMAP_CLIP; policy <= PVR_TILEMAP_CLAMP; ++policy) {
        view.address_x = view.address_y = policy;
        for(unsigned rot = 0; rot < 4; ++rot) {
            view.rotation = rot * 0.47f;
            view.scale_x = 0.73f;
            view.scale_y = 1.17f;
            view.anchor_x = 5.5f;
            view.anchor_y = 3.75f;
            result = compile(&map, &view);
            coverage(&map, &view, &result);
            for(size_t d = 0; d < result.visible_tiles; ++d) {
                int x = draws[d].column, y = draws[d].row;
                if(policy == PVR_TILEMAP_WRAP) {
                    x = (x % 3 + 3) % 3; y = (y % 2 + 2) % 2;
                }
                else if(policy == PVR_TILEMAP_CLAMP) {
                    x = x < 0 ? 0 : x > 2 ? 2 : x;
                    y = y < 0 ? 0 : y > 1 ? 1 : y;
                }
                assert(draws[d].tile_index == indices[y * 4 + x]);
            }
        }
    }
    for(int x = PVR_TILEMAP_CLIP; x <= PVR_TILEMAP_CLAMP; ++x) {
        for(int y = PVR_TILEMAP_CLIP; y <= PVR_TILEMAP_CLAMP; ++y) {
            view.address_x = x;
            view.address_y = y;
            view.rotation = -0.29f;
            result = compile(&map, &view);
            coverage(&map, &view, &result);
        }
    }
    view = base_view();
    view.scroll_x = 1000000;
    view.max_candidates = 1;
    result = compile(&map, &view);
    assert(result.candidate_tiles == 0 && result.visible_tiles == 0);
    assert(pvr_tilemap_compile(NULL, 0, NULL, 0, &map, &view, &result) == 0);
}

static void expect_failure(const pvr_tilemap_t *map, const pvr_tilemap_view_t *view,
                           size_t vcap, size_t dcap, int error) {
    pvr_tilemap_result_t result = { 91, 92, 93 };
    memset(vertices, 0xa5, sizeof(vertices));
    memset(draws, 0x5a, sizeof(draws));
    memcpy(saved_vertices, vertices, sizeof(vertices));
    memcpy(saved_draws, draws, sizeof(draws));
    errno = 0;
    assert(pvr_tilemap_compile(vertices, vcap, draws, dcap, map, view, &result) == -1);
    assert(errno == error);
    assert(!memcmp(vertices, saved_vertices, sizeof(vertices)));
    assert(!memcmp(draws, saved_draws, sizeof(draws)));
    assert(result.candidate_tiles == 91 && result.visible_tiles == 92 &&
           result.vertex_count == 93);
}

static void test_admission(void) {
    pvr_tilemap_t map = base_map();
    pvr_tilemap_view_t view = base_view();
    expect_failure(&map, &view, 23, 512, ENOSPC);
    expect_failure(&map, &view, 8192, 5, ENOSPC);
    indices[6] = 9;
    expect_failure(&map, &view, 8192, 512, EILSEQ);
    indices[6] = 1;
    view.max_candidates = 1;
    expect_failure(&map, &view, 8192, 512, E2BIG);
    view = base_view();
    view.scale_x = 0;
    expect_failure(&map, &view, 8192, 512, EINVAL);
    view.scale_x = NAN;
    expect_failure(&map, &view, 8192, 512, EINVAL);
    view = base_view();
    view.scroll_x = FLT_MAX;
    expect_failure(&map, &view, 8192, 512, ERANGE);
    view = base_view();
    map.index_count = 6;
    expect_failure(&map, &view, 8192, 512, EINVAL);
    map = base_map();
    map.row_stride = SIZE_MAX;
    expect_failure(&map, &view, 8192, 512, ERANGE);
    map = base_map();
    map.tile_count = SIZE_MAX;
    expect_failure(&map, &view, 8192, 512, ERANGE);
    map = base_map();
    tiles[1].list = PVR_LIST_TR_MOD;
    expect_failure(&map, &view, 8192, 512, EILSEQ);
    map = base_map();
    cells[1].u1 = NAN;
    expect_failure(&map, &view, 8192, 512, EILSEQ);
    cells[1].u1 = 1;
    pvr_tilemap_result_t result;
    assert(pvr_tilemap_compile(vertices, 8192,
           (pvr_tilemap_draw_t *)vertices, 512, &map, &view, &result) == -1);
    assert(errno == EINVAL);
    assert(pvr_tilemap_measure(&map, &view, (pvr_tilemap_result_t *)&map) == -1);
    assert(errno == EINVAL);
    /* Rejected off-screen indices, including padding, are never scanned. */
    indices[6] = 9;
    view.right = 0.5f; view.bottom = 0.5f;
    result = compile(&map, &view);
    assert(result.visible_tiles == 1);
    indices[6] = 1;
    tiles[1].flags = PVR_CELL_HIDDEN;
    view = base_view();
    result = compile(&map, &view);
    assert(result.visible_tiles == 3);
    indices[0] = PVR_TILEMAP_EMPTY;
    result = compile(&map, &view);
    assert(result.visible_tiles == 2);
    indices[0] = 0;
}

static void test_interpolation(void) {
    pvr_tilemap_t map = base_map();
    pvr_tilemap_view_t view = base_view();
    view.left = view.top = 1;
    view.right = view.bottom = 3;
    /* Red increases along X; offset red increases along Y. These fields
       exercise interpolation separately from the coverage oracle. */
    tiles[0].argb[0] = tiles[0].argb[1] = 0xff000000;
    tiles[0].argb[2] = tiles[0].argb[3] = 0xfff00000;
    tiles[0].oargb[0] = tiles[0].oargb[3] = 0x00f00000;
    tiles[0].oargb[1] = tiles[0].oargb[2] = 0;
    pvr_tilemap_result_t result = compile(&map, &view);
    assert(result.visible_tiles == 1 && result.vertex_count >= 6);
    for(size_t i = 0; i < result.vertex_count; ++i) {
        const pvr_vertex_t *v = vertices + i;
        assert(fabsf(v->u - v->x * 0.125f) < 0.00001f);
        assert(fabsf(v->v - v->y * 0.25f) < 0.00001f);
        assert(fabsf((float)((v->argb >> 16) & 255) - v->x * 60) <= 2);
        assert(fabsf((float)((v->oargb >> 16) & 255) - v->y * 60) <= 2);
    }
    coverage(&map, &view, &result);
}

int main(void) {
    test_maps();
    test_interpolation();
    test_admission();
    puts("pvr-tilemap-test: PASS");
    return 0;
}
