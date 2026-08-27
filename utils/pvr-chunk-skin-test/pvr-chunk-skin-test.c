/* KallistiOS ##version##

   Host-side compact-model skin binding tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_skin.h>

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

static const pvr_chunk_skin_influence_t valid_influences[] = {
    { 0, { 0, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 1, { 1, 0, 0, 0 }, { UINT16_MAX, 0, 0, 0 }, 0 },
    { 2, { 0, 1, 0, 0 }, { 32768, 32767, 0, 0 }, 0 }
};

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.0002f;
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

    lookup[1] = PVR_CHUNK_SKIN_INDEX_NONE;
    errno = 0;
    assert(pvr_chunk_skin_source_build(&binding, source_workspace,
                                       sizeof(source_workspace),
                                       &source) == -1);
    assert(errno == EILSEQ && source.vertices == NULL);

    puts("pvr-chunk-skin-test: PASS");
    return 0;
}
