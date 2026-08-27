/* KallistiOS ##version##

   Host-side compact-model shape binding tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_shape.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VERTEX_HEADER(type, size) ((uint32_t)(type) | \
                                   ((uint32_t)(size) << 16))

static const uint32_t vertices[] = {
    VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ_NORMAL, 19),
    UINT32_C(0x00030000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x3f800000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x40000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000000), UINT32_C(0x3f800000),
    UINT32_C(0x000000ff)
};

static const uint16_t polygons[] = {
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static const pvr_chunk_shape_delta_t target0_deltas[] = {
    {
        1, 0,
        {
            { 2.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.0f }
        }
    }
};

static const pvr_chunk_shape_delta_t target1_deltas[] = {
    {
        2, 0,
        {
            { 0.0f, 4.0f, 0.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f }
        }
    }
};

static const pvr_chunk_shape_target_t valid_targets[] = {
    { target0_deltas, 1 },
    { target1_deltas, 1 }
};

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.0002f;
}

int main(void) {
    const pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]),
        { 1.0f, 0.0f, 0.0f }, 2.0f
    };
    const pvr_chunk_shape_set_t shapes = {
        valid_targets, sizeof(valid_targets) / sizeof(valid_targets[0])
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_shape_requirements_t requirements;
    pvr_chunk_shape_binding_t binding;
    uint32_t lookup[256];
    alignas(32) uint8_t source_workspace[288];
    pvr_chunk_shape_source_t source;
    const pvr_chunk_shape_channel_t channels[] = {
        { NULL, 0.5f },
        { NULL, 0.25f }
    };
    anim_morph_target_tracks_t tracks[2];
    pvr_morph_target_t sampled[2];
    pvr_deform_vertex_t output[3];
    pvr_deform_result_t result;
    pvr_chunk_shape_pose_t pose;
    pvr_deform_vertex_t resolved;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);

    assert(pvr_chunk_shape_query(&plan, 2, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.lookup_entries == 256);
    assert(requirements.lookup_bytes == sizeof(lookup));
    assert(requirements.source_vertices == 3);
    assert(requirements.source_targets == 2);
    assert(requirements.source_bytes == sizeof(source_workspace));

    memset(lookup, 0x5a, sizeof(lookup));
    {
        pvr_chunk_shape_delta_t malformed[2] = {
            target0_deltas[0], target0_deltas[0]
        };
        const pvr_chunk_shape_target_t invalid_targets[] = {
            { malformed, 2 }
        };
        const pvr_chunk_shape_set_t invalid = { invalid_targets, 1 };

        errno = 0;
        assert(pvr_chunk_shape_bind(&plan, &invalid, lookup, 256,
                                    &binding) == -1);
        assert(errno == EEXIST);
        assert(lookup[0] == UINT32_C(0x5a5a5a5a));
        assert(binding.shapes.targets == NULL);
    }

    assert(pvr_chunk_shape_bind(&plan, &shapes, lookup, 256,
                                &binding) == 0);
    assert(lookup[0] == 0 && lookup[1] == 1 && lookup[2] == 2);
    assert(lookup[3] == PVR_CHUNK_SHAPE_INDEX_NONE);
    assert(pvr_chunk_shape_source_build(&binding, source_workspace,
                                        sizeof(source_workspace),
                                        &source) == 0);
    assert(source.vertex_count == 3 && source.target_count == 2);
    assert(source.vertices[2].position.x == 2.0f);
    assert(source.deltas[0].position.x == 0.0f);
    assert(source.deltas[1].position.x == 2.0f);
    assert(source.deltas[5].position.y == 4.0f);

    assert(pvr_chunk_shape_motion_bind(&source, channels, 2, tracks, 2) == 0);
    assert(tracks[0].fallback.deltas == source.deltas);
    assert(tracks[1].fallback.deltas == source.deltas + 3);
    assert(tracks[0].fallback.weight == 0.5f);
    sampled[0] = tracks[0].fallback;
    sampled[1] = tracks[1].fallback;

    assert(pvr_chunk_shape_apply(&source, sampled, 2, output, 3,
                                 &result) == 0);
    assert(result.deformed_vertices == 3);
    assert(close_enough(output[0].position.x, 0.0f));
    assert(close_enough(output[1].position.x, 2.0f));
    assert(close_enough(output[2].position.x, 2.0f));
    assert(close_enough(output[2].position.y, 1.0f));
    assert(close_enough(output[2].normal.x, 0.2425356f));
    assert(close_enough(output[2].normal.z, 0.9701425f));

    pose.binding = &binding;
    pose.vertices = output;
    pose.vertex_count = 3;
    assert(pvr_chunk_shape_pose_vertex_get(&pose, 2, &resolved) == 0);
    assert(close_enough(resolved.position.y, 1.0f));
    errno = 0;
    assert(pvr_chunk_shape_pose_vertex_get(&pose, 3, &resolved) == -1);
    assert(errno == ENOENT);

    {
        pvr_morph_target_t reordered[2] = { sampled[1], sampled[0] };

        errno = 0;
        assert(pvr_chunk_shape_apply(&source, reordered, 2, output, 3,
                                     &result) == -1);
        assert(errno == EINVAL && result.deformed_vertices == 0);
    }

    lookup[1] = PVR_CHUNK_SHAPE_INDEX_NONE;
    errno = 0;
    assert(pvr_chunk_shape_source_build(&binding, source_workspace,
                                        sizeof(source_workspace),
                                        &source) == -1);
    assert(errno == EILSEQ && source.vertices == NULL);

    puts("pvr-chunk-shape-test: PASS");
    return 0;
}
