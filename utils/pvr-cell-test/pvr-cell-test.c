/* KallistiOS ##version##

   Host-side cell-sprite animation and composition tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_cell.h>
#include <dc/pvr_cell_asset.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __DREAMCAST__
int pvr_prim(const void *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    (void)data;
    (void)size;
    return 0;
}
#endif

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.0001f;
}

static float edge_value(const pvr_vertex_t *a, const pvr_vertex_t *b,
                        float x, float y) {
    return (x - a->x) * (b->y - a->y) -
           (y - a->y) * (b->x - a->x);
}

static int point_in_triangle(const pvr_vertex_t *a,
                             const pvr_vertex_t *b,
                             const pvr_vertex_t *c,
                             float x, float y) {
    float ab = edge_value(a, b, x, y);
    float bc = edge_value(b, c, x, y);
    float ca = edge_value(c, a, x, y);

    return (ab >= 0.0f && bc >= 0.0f && ca >= 0.0f) ||
           (ab <= 0.0f && bc <= 0.0f && ca <= 0.0f);
}

static int quad_strip_matches_coverage_golden(const pvr_vertex_t strip[4],
                                              float left, float top,
                                              float right, float bottom) {
    enum {
        RASTER_WIDTH = 16,
        RASTER_HEIGHT = 8
    };
    uint16_t coverage[RASTER_HEIGHT] = { 0 };
    uint16_t overlap[RASTER_HEIGHT] = { 0 };
    size_t y;

    for(y = 0; y < RASTER_HEIGHT; ++y) {
        size_t x;

        for(x = 0; x < RASTER_WIDTH; ++x) {
            /* Unequal subpixel offsets keep every sample off the shared
               diagonal, making exact single coverage the stable golden. */
            float sample_x = left + (right - left) *
                ((float)x + 0.37f) / (float)RASTER_WIDTH;
            float sample_y = top + (bottom - top) *
                ((float)y + 0.61f) / (float)RASTER_HEIGHT;
            unsigned count =
                (unsigned)point_in_triangle(
                    &strip[0], &strip[1], &strip[2], sample_x, sample_y) +
                (unsigned)point_in_triangle(
                    &strip[1], &strip[2], &strip[3], sample_x, sample_y);

            if(count)
                coverage[y] |= (uint16_t)(UINT16_C(1) << x);
            if(count > 1u)
                overlap[y] |= (uint16_t)(UINT16_C(1) << x);
        }
    }
    for(y = 0; y < RASTER_HEIGHT; ++y) {
        if(coverage[y] != UINT16_MAX || overlap[y])
            return 0;
    }
    return 1;
}

static uint32_t crc32_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    for(index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void refresh_asset_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 44, crc32_bytes(
        bytes + PVR_CELL_ASSET_HEADER_BYTES,
        size - PVR_CELL_ASSET_HEADER_BYTES));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
}

static pvr_cell_state_t make_state(size_t atlas_cell, float x, float y,
                                   int32_t priority, uint32_t material) {
    pvr_cell_state_t state = {
        .atlas_cell_index = atlas_cell,
        .offset = { x, y, 0.0f, 0.0f },
        .rotation = 0.25f,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .priority = priority,
        .flags = PVR_CELL_NONE,
        .material_id = material,
        .argb = {
            UINT32_C(0x80ffffff), UINT32_C(0x80ffffff),
            UINT32_C(0x80ffffff), UINT32_C(0x80ffffff)
        },
        .oargb = {
            UINT32_C(0xffffffff), UINT32_C(0xffffffff),
            UINT32_C(0xffffffff), UINT32_C(0xffffffff)
        }
    };

    return state;
}

static void test_cell_asset(void) {
    pvr_cell_state_t base[2] = {
        make_state(0, 1.0f, 2.0f, -2, 4),
        make_state(1, 3.0f, 4.0f, 5, 6)
    };
    pvr_cell_key_t keys[2] = {
        {
            .time = 0.25f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_ATLAS_CELL | PVR_CELL_KEY_OFFSET |
                      PVR_CELL_KEY_DIFFUSE,
            .value = {
                .atlas_cell_index = 7,
                .offset = { 8.0f, 9.0f, 10.0f, 0.0f },
                .argb = {
                    UINT32_C(0xff102030), UINT32_C(0xff405060),
                    UINT32_C(0xff708090), UINT32_C(0xffa0b0c0)
                }
            }
        },
        {
            .time = 0.75f,
            .slot_index = 1,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_HIDDEN }
        }
    };
    pvr_cell_stream_t source_streams[2] = {
        { keys, 2, 0.125f, 1.0f, 1 },
        { NULL, 0, -0.25f, 2.0f, 0 }
    };
    pvr_cell_asset_view_t view;
    pvr_cell_asset_stream_t stream;
    pvr_cell_asset_runtime_t runtime;
    pvr_cell_state_t decoded_cells[2];
    pvr_cell_key_t decoded_keys[2];
    pvr_cell_stream_view_t decoded_streams[2];
    pvr_cell_state_t sampled[2];
    pvr_cell_state_t workspace[2];
    pvr_cell_sprite_t sprite;
    pvr_cell_key_t key;
    uint8_t *asset;
    uint8_t *corrupt;
    uint8_t base_only[PVR_CELL_ASSET_HEADER_BYTES +
                      PVR_CELL_ASSET_STATE_BYTES];
    pvr_cell_asset_view_t base_only_view;
    pvr_cell_asset_runtime_t base_only_runtime;
    pvr_cell_state_t base_only_cell;
    size_t required;
    size_t written = 0;

    assert(pvr_cell_asset_measure(base, 2, source_streams, 2,
                                  &required) == 0);
    assert(required == PVR_CELL_ASSET_HEADER_BYTES +
                       2 * PVR_CELL_ASSET_STATE_BYTES +
                       2 * PVR_CELL_ASSET_STREAM_BYTES +
                       2 * PVR_CELL_ASSET_KEY_BYTES);
    asset = malloc(required);
    corrupt = malloc(required);
    assert(asset && corrupt);
    assert(pvr_cell_asset_encode(base, 2, source_streams, 2,
                                 asset, required, &written) == 0);
    assert(written == required);
    assert(pvr_cell_asset_open(asset, required, &view) == 0);
    assert(view.cell_count == 2 && view.stream_count == 2 &&
           view.key_count == 2);

    assert(pvr_cell_asset_state_get(&view, 1, &decoded_cells[1]) == 0);
    assert(decoded_cells[1].atlas_cell_index == 1 &&
           close_enough(decoded_cells[1].offset.y, 4.0f) &&
           decoded_cells[1].priority == 5 &&
           decoded_cells[1].material_id == 6);
    assert(pvr_cell_asset_stream_get(&view, 1, &stream) == 0);
    assert(stream.first_key == 2 && stream.key_count == 0 &&
           close_enough(stream.time_offset, -0.25f) &&
           close_enough(stream.time_max, 2.0f) && !stream.repeat);
    assert(pvr_cell_asset_key_get(&view, 0, &key) == 0);
    assert(key.slot_index == 0 &&
           key.fields == (PVR_CELL_KEY_ATLAS_CELL | PVR_CELL_KEY_OFFSET |
                          PVR_CELL_KEY_DIFFUSE) &&
           key.value.atlas_cell_index == 7 &&
           close_enough(key.value.offset.z, 10.0f) &&
           key.value.argb[3] == UINT32_C(0xffa0b0c0) &&
           key.value.scale_x == 0.0f);

    memset(&runtime, 0x5a, sizeof(runtime));
    {
        pvr_cell_asset_runtime_t unchanged = runtime;

        errno = 0;
        assert(pvr_cell_asset_materialize(
            &view, decoded_cells, 1, decoded_keys, 2,
            decoded_streams, 2, &runtime) == -1);
        assert(errno == ENOSPC &&
               !memcmp(&runtime, &unchanged, sizeof(runtime)));
    }
    assert(pvr_cell_asset_materialize(
        &view, decoded_cells, 2, decoded_keys, 2,
        decoded_streams, 2, &runtime) == 0);
    assert(runtime.base_cells == decoded_cells && runtime.cell_count == 2 &&
           runtime.stream_list.streams == decoded_streams &&
           runtime.stream_list.stream_count == 2);

    sprite.base_cells = runtime.base_cells;
    sprite.cell_count = runtime.cell_count;
    sprite.position = (point_t){ 0.0f, 0.0f, 0.0f, 1.0f };
    sprite.rotation = 0.0f;
    sprite.scale_x = 1.0f;
    sprite.scale_y = 1.0f;
    sprite.argb = UINT32_MAX;
    sprite.oargb = UINT32_MAX;
    assert(pvr_cell_stream_list_sample(
        &sprite, &runtime.stream_list, 0.5f, sampled, workspace, 2,
        NULL) == 0);
    assert(sampled[0].atlas_cell_index == 7 &&
           close_enough(sampled[0].offset.x, 8.0f) &&
           sampled[0].argb[0] == UINT32_C(0xff102030) &&
           !(sampled[1].flags & PVR_CELL_HIDDEN));

    errno = 0;
    assert(pvr_cell_asset_encode(base, 2, source_streams, 2,
                                 base, required, NULL) == -1);
    assert(errno == EINVAL);
    memcpy(corrupt, asset, required);
    corrupt[required - 1u] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_cell_asset_open(corrupt, required, &view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt, asset, required);
    corrupt[PVR_CELL_ASSET_HEADER_BYTES +
            2 * PVR_CELL_ASSET_STATE_BYTES +
            2 * PVR_CELL_ASSET_STREAM_BYTES + 16 + 20] = 1;
    refresh_asset_crc(corrupt, required);
    errno = 0;
    assert(pvr_cell_asset_open(corrupt, required, &view) == -1);
    assert(errno == EILSEQ);

    assert(pvr_cell_asset_encode(base, 1, NULL, 0, base_only,
                                 sizeof(base_only), NULL) == 0);
    assert(pvr_cell_asset_open(base_only, sizeof(base_only),
                               &base_only_view) == 0);
    assert(pvr_cell_asset_materialize(
        &base_only_view, &base_only_cell, 1, NULL, 0, NULL, 0,
        &base_only_runtime) == 0);
    assert(base_only_runtime.cell_count == 1 &&
           base_only_runtime.stream_list.streams == NULL &&
           base_only_runtime.stream_list.stream_count == 0);

    free(corrupt);
    free(asset);
}

static void test_streams_and_composition(void) {
    pvr_cell_state_t base[2] = {
        make_state(0, 1.0f, 0.0f, 5, 1),
        make_state(1, 0.0f, 2.0f, 0, 2)
    };
    pvr_cell_key_t body_keys[3] = {
        {
            .time = 0.0f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_OFFSET,
            .value = { .offset = { 2.0f, 0.0f, 0.0f, 0.0f } }
        },
        {
            .time = 0.5f,
            .slot_index = 1,
            .fields = PVR_CELL_KEY_FLAGS,
            .value = { .flags = PVR_CELL_HIDDEN }
        },
        {
            .time = 0.5f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_PRIORITY,
            .value = { .priority = -3 }
        }
    };
    pvr_cell_key_t face_keys[2] = {
        {
            .time = 0.2f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_MATERIAL,
            .value = { .material_id = 7 }
        },
        {
            .time = 0.75f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_ROTATION,
            .value = { .rotation = 1.0f }
        }
    };
    pvr_cell_stream_t body = {
        body_keys, 3, 0.0f, 1.0f, 0
    };
    pvr_cell_stream_t face = {
        face_keys, 2, 0.25f, 1.0f, 1
    };
    pvr_cell_stream_view_t views[2];
    pvr_cell_stream_list_t list = { views, 2 };
    pvr_cell_sprite_t sprite = {
        .base_cells = base,
        .cell_count = 2,
        .position = { 10.0f, 20.0f, 1.0f, 0.0f },
        .rotation = 1.57079632679489661923f,
        .scale_x = 2.0f,
        .scale_y = 3.0f,
        .argb = UINT32_C(0xffffffff),
        .oargb = UINT32_C(0x00000000)
    };
    pvr_cell_state_t sampled[2];
    pvr_cell_state_t workspace[2];
    pvr_cell_resolved_t resolved[2];
    pvr_cell_sample_result_t sample_result;
    pvr_cell_resolve_result_t resolve_result;
    pvr_sprite_instance_stream_t instance_stream;

    assert(pvr_cell_stream_open(&body, 2, &views[0]) == 0);
    assert(pvr_cell_stream_open(&face, 2, &views[1]) == 0);
    assert(pvr_cell_stream_list_sample(&sprite, &list, 0.5f, sampled,
                                       workspace, 2, &sample_result) == 0);
    assert(sample_result.sampled_streams == 2);
    assert(sample_result.applied_keys == 5);
    assert(sample_result.published_cells == 2);
    assert(close_enough(sampled[0].offset.x, 2.0f));
    assert(sampled[0].priority == -3);
    assert(sampled[0].material_id == 7);
    assert(close_enough(sampled[0].rotation, 1.0f));
    assert(sampled[1].flags == PVR_CELL_HIDDEN);

    assert(pvr_cell_sprite_resolve(&sprite, sampled, 2, resolved, 2,
                                   &resolve_result) == 0);
    assert(resolve_result.examined_cells == 2);
    assert(resolve_result.resolved_cells == 2);
    assert(resolve_result.visible_cells == 1);
    assert(close_enough(resolved[0].instance.position.x, 10.0f));
    assert(close_enough(resolved[0].instance.position.y, 24.0f));
    assert(close_enough(resolved[0].instance.position.z, 1.0f));
    assert(close_enough(resolved[0].instance.scale_x, 2.0f));
    assert(close_enough(resolved[0].instance.scale_y, 3.0f));
    assert(close_enough(resolved[0].instance.rotation,
                        2.57079632679489661923f));
    assert(resolved[0].argb[0] == UINT32_C(0x80ffffff));
    assert(resolved[0].oargb[0] == 0);
    assert(resolved[0].slot_index == 0 && resolved[0].material_id == 7);

    assert(pvr_cell_resolved_stream(resolved, 2, &instance_stream) == 0);
    assert(instance_stream.instances == resolved);
    assert(instance_stream.instance_count == 2);
    assert(instance_stream.stride == sizeof(pvr_cell_resolved_t));

    resolved[0].priority = 9;
    resolved[1].priority = -2;
    assert(pvr_cell_resolved_sort(resolved, 2) == 0);
    assert(resolved[0].slot_index == 1 && resolved[1].slot_index == 0);
}

static void test_stream_time_policy(void) {
    pvr_cell_state_t state = make_state(0, 0.0f, 0.0f, 0, 0);
    pvr_cell_key_t key = {
        .time = 0.75f,
        .slot_index = 0,
        .fields = PVR_CELL_KEY_PRIORITY,
        .value = { .priority = 11 }
    };
    pvr_cell_stream_t source = { &key, 1, 0.0f, 1.0f, 1 };
    pvr_cell_stream_view_t view;
    size_t applied = SIZE_MAX;

    assert(pvr_cell_stream_open(&source, 1, &view) == 0);
    assert(pvr_cell_stream_sample(&view, 1.5f, &state, 1, &applied) == 0);
    assert(applied == 0 && state.priority == 0);
    assert(pvr_cell_stream_sample(&view, -0.25f, &state, 1, &applied) == 0);
    assert(applied == 1 && state.priority == 11);

    state.priority = 0;
    source.repeat = 0;
    assert(pvr_cell_stream_open(&source, 1, &view) == 0);
    assert(pvr_cell_stream_sample(&view, 2.0f, &state, 1, &applied) == 0);
    assert(applied == 1 && state.priority == 11);
}

static void test_compile_and_failures(void) {
    pvr_cell_state_t base[2] = {
        make_state(0, 0.0f, 0.0f, 0, 0),
        make_state(1, 4.0f, 0.0f, 1, 0)
    };
    pvr_cell_sprite_t sprite = {
        .base_cells = base,
        .cell_count = 2,
        .position = { 100.0f, 80.0f, 1.0f, 0.0f },
        .rotation = 0.0f,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .argb = UINT32_MAX,
        .oargb = UINT32_MAX
    };
    pvr_sprite_cell_t atlas_cells[2] = {
        { 16.0f, 8.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 1.0f },
        { 16.0f, 8.0f, 0.5f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f }
    };
    pvr_sprite_atlas_t atlas = { atlas_cells, 2 };
    pvr_cell_resolved_t resolved[2];
    alignas(32) pvr_sprite_txr_t packets[2];
    alignas(32) pvr_vertex_t colored[8];
    alignas(32) const matrix_t identity = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };
    const pvr_sprite_billboard_basis_t basis = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f }
    };
    pvr_sprite_batch_result_t result;
    pvr_cell_key_t invalid_keys[2] = {
        {
            .time = 0.5f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_PRIORITY,
            .value = { .priority = 1 }
        },
        {
            .time = 0.25f,
            .slot_index = 0,
            .fields = PVR_CELL_KEY_PRIORITY,
            .value = { .priority = 2 }
        }
    };
    pvr_cell_stream_t invalid = { invalid_keys, 2, 0.0f, 1.0f, 0 };
    pvr_cell_stream_view_t unchanged;
    pvr_cell_stream_view_t before;

    base[0].rotation = 0.0f;
    base[1].rotation = 0.0f;
    assert(pvr_cell_sprite_resolve(&sprite, base, 2, resolved, 2, NULL) == 0);
    resolved[1].instance.flags = PVR_CELL_HIDDEN;
    assert(pvr_cell_sprite_compile_2d(packets, 2, &atlas, resolved, 2,
                                      &result) == 0);
    assert(result.examined_instances == 2 && result.produced_sprites == 1);
    assert(close_enough(packets[0].ax, 92.0f));
    assert(close_enough(packets[0].ay, 84.0f));

    resolved[0].argb[0] = UINT32_C(0xff112233);
    resolved[0].argb[1] = UINT32_C(0xff445566);
    resolved[0].argb[2] = UINT32_C(0xff778899);
    resolved[0].argb[3] = UINT32_C(0xffaabbcc);
    assert(pvr_cell_sprite_compile_colored_2d(colored, 8, &atlas, resolved,
                                              2, &result) == 0);
    assert(result.examined_instances == 2 && result.produced_sprites == 1);
    assert(close_enough(colored[0].x, 92.0f));
    assert(close_enough(colored[0].y, 84.0f));
    assert(colored[0].argb == UINT32_C(0xff112233));
    assert(colored[1].argb == UINT32_C(0xff445566));
    assert(colored[2].argb == UINT32_C(0xffaabbcc));
    assert(colored[3].argb == UINT32_C(0xff778899));
    assert(colored[0].flags == PVR_CMD_VERTEX);
    assert(colored[3].flags == PVR_CMD_VERTEX_EOL);
    assert(close_enough(colored[0].u, 0.0f));
    assert(close_enough(colored[0].v, 1.0f));
    assert(close_enough(colored[2].x, 108.0f));
    assert(close_enough(colored[2].y, 84.0f));
    assert(close_enough(colored[2].u, 0.5f));
    assert(close_enough(colored[2].v, 1.0f));
    assert(close_enough(colored[3].x, 108.0f));
    assert(close_enough(colored[3].y, 76.0f));
    assert(close_enough(colored[3].u, 0.5f));
    assert(close_enough(colored[3].v, 0.0f));
    assert(quad_strip_matches_coverage_golden(
        colored, 92.0f, 76.0f, 108.0f, 84.0f));
    {
        pvr_vertex_t rectangle_order[4] = {
            colored[0], colored[1], colored[3], colored[2]
        };

        assert(!quad_strip_matches_coverage_golden(
            rectangle_order, 92.0f, 76.0f, 108.0f, 84.0f));
    }

    assert(pvr_cell_sprite_compile_colored_3d(colored, 8, &atlas, resolved,
                                              2, &basis, &identity,
                                              &result) == 0);
    assert(result.produced_sprites == 1);
    assert(colored[0].argb == UINT32_C(0xff112233));
    assert(colored[2].argb == UINT32_C(0xffaabbcc));
    assert(colored[3].argb == UINT32_C(0xff778899));

    memset(&unchanged, 0x5a, sizeof(unchanged));
    before = unchanged;
    errno = 0;
    assert(pvr_cell_stream_open(&invalid, 1, &unchanged) == -1);
    assert(errno == EINVAL);
    assert(memcmp(&unchanged, &before, sizeof(unchanged)) == 0);

    errno = 0;
    assert(pvr_cell_sprite_resolve(&sprite, base, 2, resolved, 1,
                                   NULL) == -1);
    assert(errno == EINVAL);
}

static void test_motion_and_events(void) {
    pvr_cell_state_t base = make_state(0, 0.0f, 0.0f, 0, 0);
    pvr_cell_sprite_t sprite = {
        .base_cells = &base,
        .cell_count = 1,
        .position = { 10.0f, 20.0f, 0.5f, 0.0f },
        .rotation = 0.25f,
        .scale_x = 2.0f,
        .scale_y = 3.0f,
        .argb = UINT32_MAX,
        .oargb = UINT32_MAX
    };
    anim_transform_t transform = {
        .translation = { 1.0f, 2.0f, 0.25f, 0.0f },
        .rotation = {
            0.70710678118654752440f, 0.0f, 0.0f,
            0.70710678118654752440f
        },
        .scale = { 0.5f, 2.0f, 1.0f, 0.0f }
    };
    pvr_cell_sprite_t composed;
    pvr_cell_sprite_t unchanged;
    const pvr_cell_stream_t source = { NULL, 0, 0.25f, 1.0f, 1 };
    pvr_cell_stream_view_t stream;
    const anim_event_key_t event_keys[2] = {
        { 0.0f, 10, 100 },
        { 0.5f, 20, 200 }
    };
    const anim_event_track_view_t events = {
        { event_keys, 2 }, 0.0f, 0.5f
    };
    anim_event_occurrence_t occurrences[3];
    anim_event_result_t event_result;

    assert(pvr_cell_sprite_apply_transform(&sprite, &transform,
                                           &composed) == 0);
    assert(composed.base_cells == sprite.base_cells);
    assert(close_enough(composed.position.x, 11.0f));
    assert(close_enough(composed.position.y, 22.0f));
    assert(close_enough(composed.position.z, 0.75f));
    assert(close_enough(composed.rotation,
                        1.82079632679489661923f));
    assert(close_enough(composed.scale_x, 1.0f));
    assert(close_enough(composed.scale_y, 6.0f));

    unchanged = composed;
    transform.rotation.w = 0.70710678118654752440f;
    transform.rotation.x = 0.70710678118654752440f;
    transform.rotation.z = 0.0f;
    errno = 0;
    assert(pvr_cell_sprite_apply_transform(&sprite, &transform,
                                           &composed) == -1);
    assert(errno == ENOTSUP);
    assert(memcmp(&composed, &unchanged, sizeof(composed)) == 0);

    assert(pvr_cell_stream_open(&source, 1, &stream) == 0);
    assert(pvr_cell_stream_collect_events(&stream, 0.0f, 2.0f, &events,
                                          occurrences, 3,
                                          &event_result) == 0);
    assert(event_result.matching_events == 4);
    assert(event_result.published_events == 3);
    assert(event_result.truncated);
    assert(occurrences[0].event.identifier == 20);
    assert(occurrences[1].event.identifier == 10);
    assert(occurrences[2].event.identifier == 20);
    assert(occurrences[0].direction == ANIM_PLAYBACK_FORWARD);

    {
        const pvr_cell_stream_t clamped_source = {
            NULL, 0, 0.25f, 1.0f, 0
        };
        pvr_cell_stream_view_t clamped;

        assert(pvr_cell_stream_open(&clamped_source, 1, &clamped) == 0);
        assert(pvr_cell_stream_collect_events(&clamped, -0.5f, 0.0f,
                                              &events, occurrences, 3,
                                              &event_result) == 0);
        assert(event_result.matching_events == 1);
        assert(event_result.published_events == 1);
        assert(!event_result.truncated);
        assert(occurrences[0].event.identifier == 10);
    }

    errno = 0;
    assert(pvr_cell_stream_collect_events(&stream, 2.0f, 1.0f, &events,
                                          NULL, 0, NULL) == -1);
    assert(errno == EINVAL);
}

int main(void) {
    test_cell_asset();
    test_streams_and_composition();
    test_stream_time_policy();
    test_compile_and_failures();
    test_motion_and_events();
    puts("pvr-cell-test: PASS");
    return 0;
}
