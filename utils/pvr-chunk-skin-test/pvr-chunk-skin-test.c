/* KallistiOS ##version##

   Host-side compact-model skin binding tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_chunk_skin_asset.h>
#include <dc/pvr_chunk_skeleton_asset.h>

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

static const pvr_chunk_skin_influence_t valid_influences[] = {
    { 0, { 0, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 1, { 1, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 2, { 0, 1, 0, 0 }, { 32768, 32767, 0, 0 }, 0 }
};

static const pvr_chunk_skin_span_t general_spans[] = {
    { 0, 1, 0 },
    { 1, 1, 1 },
    { 2, 6, 2 }
};

static const pvr_chunk_skin_weight_t general_weights[] = {
    { 0, UINT16_MAX },
    { 1, UINT16_MAX },
    { 0, 10923 }, { 1, 10923 }, { 2, 10923 },
    { 3, 10923 }, { 4, 10923 }, { 5, 10920 }
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

static void refresh_skin_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 36, crc32_bytes(
        bytes + PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES,
        size - PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
}

static void refresh_skeleton_crc(uint8_t *bytes, size_t size) {
    store_le32(bytes + 28, crc32_bytes(
        bytes + PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES,
        size - PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
}

static void identity(matrix_t *matrix) {
    const matrix_t value = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f }
    };

    memcpy(matrix, &value, sizeof(value));
}

static void normal_identity(pvr_normal_matrix_t *matrix) {
    memset(matrix, 0, sizeof(*matrix));
    matrix->column[0][0] = 1.0f;
    matrix->column[1][1] = 1.0f;
    matrix->column[2][2] = 1.0f;
}

int main(void) {
    const pvr_chunk_model_t model = {
        vertices, sizeof(vertices) / sizeof(vertices[0]),
        polygons, sizeof(polygons) / sizeof(polygons[0]),
        { 1.0f, 0.0f, 0.0f }, 2.0f
    };
    const pvr_chunk_skin_t skin = {
        valid_influences,
        sizeof(valid_influences) / sizeof(valid_influences[0]), 2
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_vertex_index_entry_t plan_entries[256];
    pvr_chunk_skin_requirements_t requirements;
    pvr_chunk_skin_binding_t binding;
    uint32_t lookup[256];
    alignas(32) uint8_t source_workspace[192];
    pvr_chunk_skin_source_t source;
    alignas(8) matrix_t position_matrices[2];
    pvr_normal_matrix_t normal_matrices[2];
    pvr_skin_palette_t palette = {
        position_matrices, normal_matrices, 2
    };
    pvr_deform_vertex_t output[3];
    pvr_deform_result_t result;
    pvr_chunk_skin_pose_t pose;
    pvr_deform_vertex_t resolved;
    pvr_chunk_skin_influence_t malformed[3];

    assert(pvr_chunk_model_open(&model, &view) == 0);
    assert(pvr_chunk_model_plan_query(&view, &plan_requirements) == 0);
    assert(plan_requirements.vertex_index_entries == 256);
    assert(pvr_chunk_model_plan_build(&view, plan_entries, 256, &plan) == 0);

    assert(pvr_chunk_skin_query(&plan, &requirements) == 0);
    assert(requirements.alignment == 32);
    assert(requirements.lookup_entries == 256);
    assert(requirements.lookup_bytes == sizeof(lookup));
    assert(requirements.source_vertices == 3);
    assert(requirements.source_bytes == sizeof(source_workspace));

    memset(lookup, 0x5a, sizeof(lookup));
    memcpy(malformed, valid_influences, sizeof(malformed));
    --malformed[2].weight[1];
    {
        const pvr_chunk_skin_t invalid = { malformed, 3, 2 };

        errno = 0;
        assert(pvr_chunk_skin_bind(&plan, &invalid, lookup, 256,
                                   &binding) == -1);
        assert(errno == EILSEQ);
        assert(lookup[0] == UINT32_C(0x5a5a5a5a));
        assert(binding.skin.influences == NULL);
    }

    assert(pvr_chunk_skin_bind(&plan, &skin, lookup, 256, &binding) == 0);
    assert(lookup[0] == 0 && lookup[1] == 1 && lookup[2] == 2);
    assert(lookup[3] == PVR_CHUNK_SKIN_INDEX_NONE);
    assert(pvr_chunk_skin_source_build(&binding, source_workspace,
                                       sizeof(source_workspace),
                                       &source) == 0);
    assert(source.vertex_count == 3 && source.joint_count == 2);
    assert(source.vertices[2].position.x == 2.0f);
    assert(close_enough(source.influences[2].weight[0],
                        32768.0f / 65535.0f));

    identity(position_matrices + 0);
    identity(position_matrices + 1);
    position_matrices[1][3][0] = 10.0f;
    normal_identity(normal_matrices + 0);
    normal_identity(normal_matrices + 1);

    assert(pvr_chunk_skin_apply(&source, &palette, output, 3, &result) == 0);
    assert(result.deformed_vertices == 3);
    assert(close_enough(output[0].position.x, 0.0f));
    assert(close_enough(output[1].position.x, 11.0f));
    assert(close_enough(output[2].position.x,
                        2.0f + 10.0f * (32767.0f / 65535.0f)));
    assert(close_enough(output[2].normal.z, 1.0f));

    palette.joint_count = 1;
    errno = 0;
    assert(pvr_chunk_skin_apply(&source, &palette, output, 3,
                                &result) == -1);
    assert(errno == EINVAL && result.deformed_vertices == 0);
    palette.joint_count = 2;

    pose.binding = &binding;
    pose.vertices = output;
    pose.vertex_count = 3;
    assert(pvr_chunk_skin_pose_vertex_get(&pose, 1, &resolved) == 0);
    assert(close_enough(resolved.position.x, 11.0f));
    errno = 0;
    assert(pvr_chunk_skin_pose_vertex_get(&pose, 3, &resolved) == -1);
    assert(errno == ENOENT);

    {
        pvr_chunk_skin_general_t general_skin = {
            general_spans, 3, general_weights, 8, 6
        };
        pvr_chunk_skin_general_section_view_t section_view;
        pvr_chunk_skin_span_t decoded_spans[3];
        pvr_chunk_skin_weight_t decoded_weights[8];
        pvr_chunk_skin_general_t decoded_skin;
        pvr_chunk_skin_span_t decoded_span;
        pvr_chunk_skin_weight_t decoded_weight;
        uint8_t *serialized_skin = NULL;
        uint8_t *corrupt_skin = NULL;
        size_t serialized_bytes = 0;
        pvr_chunk_skin_general_requirements_t general_requirements;
        pvr_chunk_skin_general_binding_t general_binding;
        uint32_t general_lookup[256];
        alignas(32) uint8_t general_workspace[192];
        pvr_chunk_skin_general_source_t general_source;
        alignas(8) matrix_t general_positions[6];
        pvr_normal_matrix_t general_normals[6];
        pvr_skin_palette_t general_palette = {
            general_positions, general_normals, 6
        };
        pvr_chunk_skin_general_pose_t general_pose;
        pvr_chunk_skin_span_t malformed_spans[3];
        float expected = 2.0f;
        size_t joint;

        assert(pvr_scene_ir_serialize_general_skin(
            &general_skin, &serialized_skin, &serialized_bytes) == 0);
        assert(serialized_bytes == PVR_CHUNK_SKIN_GENERAL_HEADER_BYTES +
               3 * PVR_CHUNK_SKIN_GENERAL_SPAN_BYTES +
               8 * PVR_CHUNK_SKIN_GENERAL_WEIGHT_BYTES);
        assert(pvr_chunk_skin_general_section_open(
            serialized_skin, serialized_bytes, &section_view) == 0);
        assert(section_view.span_count == 3 &&
               section_view.weight_count == 8 &&
               section_view.joint_count == 6);
        assert(pvr_chunk_skin_general_section_span_get(
            &section_view, 2, &decoded_span) == 0);
        assert(decoded_span.vertex_index == 2 &&
               decoded_span.weight_count == 6 &&
               decoded_span.first_weight == 2);
        assert(pvr_chunk_skin_general_section_weight_get(
            &section_view, 7, &decoded_weight) == 0);
        assert(decoded_weight.joint == 5 && decoded_weight.weight == 10920);

        memset(decoded_spans, 0x5a, sizeof(decoded_spans));
        memset(decoded_weights, 0x5a, sizeof(decoded_weights));
        memset(&decoded_skin, 0x5a, sizeof(decoded_skin));
        {
            pvr_chunk_skin_general_t unchanged_skin = decoded_skin;

            errno = 0;
            assert(pvr_chunk_skin_general_section_materialize(
                &section_view, decoded_spans, 2, decoded_weights, 8,
                &decoded_skin) == -1);
            assert(errno == ENOSPC &&
                   decoded_spans[0].vertex_index == UINT16_C(0x5a5a) &&
                   decoded_weights[0].joint == UINT16_C(0x5a5a) &&
                   !memcmp(&decoded_skin, &unchanged_skin,
                           sizeof(decoded_skin)));
        }
        assert(pvr_chunk_skin_general_section_materialize(
            &section_view, decoded_spans, 3, decoded_weights, 8,
            &decoded_skin) == 0);
        assert(decoded_skin.spans == decoded_spans &&
               decoded_skin.weights == decoded_weights);

        corrupt_skin = malloc(serialized_bytes);
        assert(corrupt_skin);
        memcpy(corrupt_skin, serialized_skin, serialized_bytes);
        corrupt_skin[serialized_bytes - 1u] ^= UINT8_C(0x80);
        errno = 0;
        assert(pvr_chunk_skin_general_section_open(
            corrupt_skin, serialized_bytes, &section_view) == -1);
        assert(errno == EILSEQ);
        memcpy(corrupt_skin, serialized_skin, serialized_bytes);
        store_le32(corrupt_skin + 48 + 2 * 8 + 4, 3);
        refresh_skin_crc(corrupt_skin, serialized_bytes);
        errno = 0;
        assert(pvr_chunk_skin_general_section_open(
            corrupt_skin, serialized_bytes, &section_view) == -1);
        assert(errno == EILSEQ);
        memcpy(corrupt_skin, serialized_skin, serialized_bytes);
        corrupt_skin[48 + 3 * 8] = 6;
        corrupt_skin[48 + 3 * 8 + 1] = 0;
        refresh_skin_crc(corrupt_skin, serialized_bytes);
        errno = 0;
        assert(pvr_chunk_skin_general_section_open(
            corrupt_skin, serialized_bytes, &section_view) == -1);
        assert(errno == EILSEQ);
        free(corrupt_skin);
        corrupt_skin = NULL;

        general_skin = decoded_skin;

        assert(pvr_chunk_skin_general_query(
            &plan, &general_skin, &general_requirements) == 0);
        assert(general_requirements.lookup_entries == 256 &&
               general_requirements.source_vertices == 3 &&
               general_requirements.source_spans == 3 &&
               general_requirements.source_weights == 8 &&
               general_requirements.source_bytes ==
               sizeof(general_workspace));
        assert(pvr_chunk_skin_general_bind(
            &plan, &general_skin, general_lookup, 256,
            &general_binding) == 0);
        assert(pvr_chunk_skin_general_source_build(
            &general_binding, general_workspace,
            sizeof(general_workspace), &general_source) == 0);
        assert(general_source.vertex_count == 3 &&
               general_source.weight_count == 8 &&
               general_source.spans[2].weight_count == 6);

        for(joint = 0; joint < 6; ++joint) {
            identity(general_positions + joint);
            general_positions[joint][3][0] = (float)joint * 2.0f;
            normal_identity(general_normals + joint);
            expected += (float)joint * 2.0f *
                ((float)general_weights[2 + joint].weight / 65535.0f);
        }
        assert(pvr_chunk_skin_general_apply(
            &general_source, &general_palette, output, 3, &result) == 0);
        assert(result.deformed_vertices == 3);
        assert(close_enough(output[1].position.x, 3.0f));
        assert(close_enough(output[2].position.x, expected));

        general_pose.binding = &general_binding;
        general_pose.vertices = output;
        general_pose.vertex_count = 3;
        assert(pvr_chunk_skin_general_pose_vertex_get(
            &general_pose, 2, &resolved) == 0);
        assert(close_enough(resolved.position.x, expected));

        memcpy(malformed_spans, general_spans, sizeof(malformed_spans));
        malformed_spans[2].first_weight = 3;
        {
            const pvr_chunk_skin_general_t invalid = {
                malformed_spans, 3, general_weights, 8, 6
            };

            memset(general_lookup, 0x5a, sizeof(general_lookup));
            errno = 0;
            assert(pvr_chunk_skin_general_bind(
                &plan, &invalid, general_lookup, 256,
                &general_binding) == -1);
            assert(errno == EILSEQ &&
                   general_lookup[0] == UINT32_C(0x5a5a5a5a));
        }
        free(serialized_skin);
    }

    {
        pvr_chunk_skeleton_joint_t skeleton_joints[2];
        const pvr_chunk_skeleton_t skeleton = {
            skeleton_joints, 2, 3
        };
        pvr_chunk_skeleton_section_view_t section_view;
        pvr_chunk_skeleton_joint_t decoded_joints[2];
        pvr_chunk_skeleton_t decoded_skeleton;
        pvr_chunk_skeleton_joint_t decoded_joint;
        alignas(8) matrix_t worlds[3];
        alignas(8) matrix_t built_positions[2];
        pvr_normal_matrix_t built_normals[2];
        pvr_skin_palette_t built_palette;
        uint8_t *serialized = NULL;
        uint8_t *corrupt = NULL;
        size_t serialized_bytes = 0;
        matrix_t unchanged_position;

        identity(&skeleton_joints[0].inverse_bind);
        identity(&skeleton_joints[1].inverse_bind);
        skeleton_joints[0].node_index = 0;
        skeleton_joints[1].node_index = 2;
        skeleton_joints[1].inverse_bind[3][0] = -2.0f;

        assert(pvr_scene_ir_serialize_skeleton(
            &skeleton, &serialized, &serialized_bytes) == 0);
        assert(serialized_bytes ==
               PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES +
               2 * PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES);
        assert(pvr_chunk_skeleton_section_open(
            serialized, serialized_bytes, &section_view) == 0);
        assert(section_view.joint_count == 2 &&
               section_view.node_count == 3);
        assert(pvr_chunk_skeleton_section_joint_get(
            &section_view, 1, &decoded_joint) == 0);
        assert(decoded_joint.node_index == 2 &&
               close_enough(decoded_joint.inverse_bind[3][0], -2.0f));

        memset(decoded_joints, 0x5a, sizeof(decoded_joints));
        memset(&decoded_skeleton, 0x5a, sizeof(decoded_skeleton));
        {
            pvr_chunk_skeleton_t unchanged = decoded_skeleton;
            pvr_chunk_skeleton_joint_t unchanged_joints[2];

            memcpy(unchanged_joints, decoded_joints,
                   sizeof(unchanged_joints));

            errno = 0;
            assert(pvr_chunk_skeleton_section_materialize(
                &section_view, decoded_joints, 1,
                &decoded_skeleton) == -1);
            assert(errno == ENOSPC &&
                   !memcmp(decoded_joints, unchanged_joints,
                           sizeof(decoded_joints)) &&
                   !memcmp(&decoded_skeleton, &unchanged,
                           sizeof(decoded_skeleton)));
        }
        assert(pvr_chunk_skeleton_section_materialize(
            &section_view, decoded_joints, 2, &decoded_skeleton) == 0);
        assert(decoded_skeleton.joints == decoded_joints &&
               decoded_skeleton.node_count == 3);

        identity(&worlds[0]);
        identity(&worlds[1]);
        identity(&worlds[2]);
        worlds[0][3][0] = 3.0f;
        worlds[2][3][0] = 7.0f;
        assert(pvr_chunk_skeleton_palette_build(
            &decoded_skeleton, worlds, 3, built_positions, 2,
            built_normals, 2, &built_palette) == 0);
        assert(built_palette.position_matrices == built_positions &&
               built_palette.normal_matrices == built_normals &&
               built_palette.joint_count == 2);
        assert(close_enough(built_positions[0][3][0], 3.0f) &&
               close_enough(built_positions[1][3][0], 5.0f) &&
               close_enough(built_normals[1].column[2][2], 1.0f));

        memcpy(&unchanged_position, &built_positions[0],
               sizeof(unchanged_position));
        memset(&worlds[2], 0, sizeof(worlds[2]));
        errno = 0;
        assert(pvr_chunk_skeleton_palette_build(
            &decoded_skeleton, worlds, 3, built_positions, 2,
            built_normals, 2, &built_palette) == -1);
        assert(errno == ERANGE && !memcmp(&built_positions[0],
                                         &unchanged_position,
                                         sizeof(unchanged_position)) &&
               built_palette.position_matrices == NULL);

        corrupt = malloc(serialized_bytes);
        assert(corrupt);
        memcpy(corrupt, serialized, serialized_bytes);
        store_le32(corrupt + PVR_CHUNK_SKELETON_SECTION_HEADER_BYTES +
                       PVR_CHUNK_SKELETON_SECTION_JOINT_BYTES,
                   0);
        refresh_skeleton_crc(corrupt, serialized_bytes);
        errno = 0;
        assert(pvr_chunk_skeleton_section_open(
            corrupt, serialized_bytes, &section_view) == -1);
        assert(errno == EILSEQ);
        free(corrupt);
        free(serialized);
    }

    lookup[1] = PVR_CHUNK_SKIN_INDEX_NONE;
    errno = 0;
    assert(pvr_chunk_skin_source_build(&binding, source_workspace,
                                       sizeof(source_workspace),
                                       &source) == -1);
    assert(errno == EILSEQ && source.vertices == NULL);

    puts("pvr-chunk-skin-test: PASS");
    return 0;
}
