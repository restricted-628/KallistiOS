/* KallistiOS ##version##

   Host-side compact-model shape binding tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_shape.h>
#include <dc/pvr_chunk_shape_asset.h>

#include "pvr-scene-ir.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static void refresh_shape_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 32, crc32_bytes(
        bytes + PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES,
        size - PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
}

int main(void) {
    const pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]),
        { 1.0f, 0.0f, 0.0f }, 2.0f
    };
    pvr_chunk_shape_set_t shapes = {
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
    pvr_chunk_shape_section_view_t section_view;
    pvr_chunk_shape_section_target_t section_target;
    pvr_chunk_shape_delta_t section_delta;
    pvr_chunk_shape_target_t decoded_targets[2];
    pvr_chunk_shape_delta_t decoded_deltas[2];
    pvr_chunk_shape_set_t decoded_shapes;
    uint8_t *serialized_shapes = NULL;
    uint8_t *corrupt_shapes = NULL;
    size_t serialized_bytes = 0;

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);

    assert(pvr_scene_ir_serialize_shapes(
        &shapes, &serialized_shapes, &serialized_bytes) == 0);
    assert(serialized_bytes == PVR_CHUNK_SHAPE_SECTION_HEADER_BYTES +
           2 * PVR_CHUNK_SHAPE_SECTION_TARGET_BYTES +
           2 * PVR_CHUNK_SHAPE_SECTION_DELTA_BYTES);
    assert(pvr_chunk_shape_section_open(
        serialized_shapes, serialized_bytes, &section_view) == 0);
    assert(section_view.target_count == 2 && section_view.delta_count == 2);
    assert(pvr_chunk_shape_section_target_get(
        &section_view, 1, &section_target) == 0);
    assert(section_target.first_delta == 1 && section_target.delta_count == 1);
    assert(pvr_chunk_shape_section_delta_get(
        &section_view, 1, &section_delta) == 0);
    assert(section_delta.vertex_index == 2 &&
           close_enough(section_delta.delta.position.y, 4.0f) &&
           close_enough(section_delta.delta.normal.x, 1.0f) &&
           section_delta.delta.position.w == 0.0f &&
           section_delta.delta.normal.w == 0.0f);

    memset(decoded_targets, 0x5a, sizeof(decoded_targets));
    memset(decoded_deltas, 0x5a, sizeof(decoded_deltas));
    memset(&decoded_shapes, 0x5a, sizeof(decoded_shapes));
    {
        pvr_chunk_shape_set_t unchanged_shapes = decoded_shapes;

        errno = 0;
        assert(pvr_chunk_shape_section_materialize(
            &section_view, decoded_targets, 1, decoded_deltas, 2,
            &decoded_shapes) == -1);
        assert(errno == ENOSPC &&
               !memcmp(&decoded_shapes, &unchanged_shapes,
                       sizeof(decoded_shapes)) &&
               decoded_deltas[0].vertex_index == UINT16_C(0x5a5a));
    }
    assert(pvr_chunk_shape_section_materialize(
        &section_view, decoded_targets, 2, decoded_deltas, 2,
        &decoded_shapes) == 0);
    assert(decoded_shapes.targets == decoded_targets &&
           decoded_shapes.target_count == 2 &&
           decoded_targets[0].deltas == decoded_deltas &&
           decoded_targets[1].deltas == decoded_deltas + 1);

    corrupt_shapes = malloc(serialized_bytes);
    assert(corrupt_shapes);
    memcpy(corrupt_shapes, serialized_shapes, serialized_bytes);
    corrupt_shapes[serialized_bytes - 1u] ^= UINT8_C(0x80);
    errno = 0;
    assert(pvr_chunk_shape_section_open(
        corrupt_shapes, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt_shapes, serialized_shapes, serialized_bytes);
    store_le32(corrupt_shapes + 48 + 8, 2);
    refresh_shape_crc(corrupt_shapes, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_shape_section_open(
        corrupt_shapes, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    memcpy(corrupt_shapes, serialized_shapes, serialized_bytes);
    store_le32(corrupt_shapes + 48 + 16 + 4, UINT32_C(0x7fc00000));
    refresh_shape_crc(corrupt_shapes, serialized_bytes);
    errno = 0;
    assert(pvr_chunk_shape_section_open(
        corrupt_shapes, serialized_bytes, &section_view) == -1);
    assert(errno == EILSEQ);
    free(corrupt_shapes);
    corrupt_shapes = NULL;

    shapes = decoded_shapes;

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

    free(serialized_shapes);

    puts("pvr-chunk-shape-test: PASS");
    return 0;
}
