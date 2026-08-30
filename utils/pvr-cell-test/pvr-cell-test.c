/* KallistiOS ##version##

   Host-side cell-sprite animation and composition tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_cell.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
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
    assert(colored[2].argb == UINT32_C(0xff778899));
    assert(colored[3].argb == UINT32_C(0xffaabbcc));
    assert(colored[0].flags == PVR_CMD_VERTEX);
    assert(colored[3].flags == PVR_CMD_VERTEX_EOL);
    assert(close_enough(colored[0].u, 0.0f));
    assert(close_enough(colored[0].v, 1.0f));

    assert(pvr_cell_sprite_compile_colored_3d(colored, 8, &atlas, resolved,
                                              2, &basis, &identity,
                                              &result) == 0);
    assert(result.produced_sprites == 1);
    assert(colored[0].argb == UINT32_C(0xff112233));

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

int main(void) {
    test_streams_and_composition();
    test_stream_time_policy();
    test_compile_and_failures();
    puts("pvr-cell-test: PASS");
    return 0;
}
