/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Included by test.c to share its matrix oracle and XMTRX sentinel.
*/
static pvr_skin_compact_joint_t compact_joints[COUNT], compact_import[COUNT];
static pvr_skin_compact_palette_t compact_palette, imported_palette;

static void vertex_compare(const pvr_deform_vertex_t *a,
                           const pvr_deform_vertex_t *b) {
    assert(close_float(a->position.x, b->position.x));
    assert(close_float(a->position.y, b->position.y));
    assert(close_float(a->position.z, b->position.z));
    assert(close_float(a->normal.x, b->normal.x));
    assert(close_float(a->normal.y, b->normal.y));
    assert(close_float(a->normal.z, b->normal.z));
    assert(a->position.w == 1 && a->normal.w == 0);
}

static void compare_compact(void) {
    const pvr_skin_palette_t legacy = {reference, reference_normal, COUNT};
    assert(pvr_chunk_skeleton_palette_build_compact(&affine, &pose,
        compact_joints, COUNT, &compact_palette) == 0);
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT,
                                           &imported_palette) == 0);
    xmtrx_check();
    for(size_t i = 0; i < COUNT; ++i) {
        for(size_t c = 0; c < 4; ++c)
            for(size_t r = 0; r < 3; ++r) {
                assert(close_float(compact_joints[i].position.elem2D[c][r],
                                   reference[i][c][r]));
                assert(close_float(compact_import[i].position.elem2D[c][r],
                                   compact_joints[i].position.elem2D[c][r]));
            }
        for(size_t c = 0; c < 3; ++c)
            for(size_t r = 0; r < 3; ++r)
                assert(close_float(compact_joints[i].normal.elem2D[c][r],
                                   reference_normal[i].column[c][r]));
    }

    /* Shared spans, repeated joints, originally zero weights with ignored
       indices, nonnormalized source weights, and a strided vertex stream. */
    const pvr_skin_weight_t weights[] = {
        {UINT16_MAX, 0, 0}, {0, 0, 0.25f}, {1, 0, 0.75f},
        {2, 0, 2.0f}, {1, 0, 0.25f}};
    const pvr_skin_span_t spans[] = {{0, 3, 0}, {1, 4, 0}, {3, 2, 0}};
    pvr_skin_span_stream_t stream = {spans, 3, sizeof(*spans), weights, 5};
    pvr_skin_prepared_span_t plan_spans[3];
    pvr_skin_weight_t plan_weights[8];
    pvr_skin_prepared_spans_t plan;
    assert(pvr_skin_spans_prepare(&stream, COUNT, plan_spans, 3, plan_weights,
                                  8, &plan) == 0);
    struct padded_vertex { pvr_deform_vertex_t vertex; float padding[4]; } source[3];
    memset(source, 0, sizeof(source));
    for(size_t i = 0; i < 3; ++i) {
        source[i].vertex.position.x = 0.5f + (float)i;
        source[i].vertex.position.y = -0.75f;
        source[i].vertex.position.z = 0.25f;
        source[i].vertex.normal.x = 0.25f;
        source[i].vertex.normal.y = 0.5f;
        source[i].vertex.normal.z = 1.0f;
        source[i].vertex.position.w = source[i].vertex.normal.w = NAN;
    }
    pvr_deform_vertex_t full[3], small[3], imported[3];
    pvr_deform_stream_t vertices = {source, 3, sizeof(*source)};
    pvr_deform_result_t result;
    assert(pvr_skin_apply_spans_prepared(full, 3, &vertices, &plan, &palette, &result) == 0);
    assert(result.deformed_vertices == 3);
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan,
                                       &compact_palette, &result) == 0);
    assert(result.deformed_vertices == 3);
    assert(pvr_skin_apply_spans_compact(imported, 3, &vertices, &plan,
                                       &imported_palette, NULL) == 0);
    for(size_t i = 0; i < 3; ++i) {
        vertex_compare(small + i, full + i);
        vertex_compare(imported + i, full + i);
        small[i] = source[i].vertex;
    }
    pvr_deform_stream_t in_place = {small, 3, sizeof(*small)};
    assert(pvr_skin_apply_spans_compact(small, 3, &in_place, &plan,
                                       &compact_palette, NULL) == 0);
    for(size_t i = 0; i < 3; ++i)
        vertex_compare(small + i, full + i);
    xmtrx_check();

    /* Only valid-prefix outputs are published on dynamic failure. */
    memset(small, 0x5a, sizeof(small));
    pvr_deform_vertex_t untouched[3];
    memcpy(untouched, small, sizeof(small));
    source[1].vertex.position.x = NAN;
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan,
        &compact_palette, &result) < 0 && errno == EDOM);
    assert(result.deformed_vertices == 1);
    vertex_compare(small, full);
    assert(memcmp(small + 1, untouched + 1, 2 * sizeof(*small)) == 0);
    source[1].vertex.position.x = 1.5f;
    source[1].vertex.normal.x = source[1].vertex.normal.y = source[1].vertex.normal.z = 0;
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan,
        &compact_palette, &result) < 0 && errno == ERANGE);
    assert(result.deformed_vertices == 1);
    source[1].vertex.normal.z = 1;
    source[1].vertex.position.x = FLT_MAX;
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan,
        &compact_palette, &result) < 0 && errno == ERANGE);
    assert(result.deformed_vertices == 1);
    assert(pvr_skin_apply_spans_compact(small, 2, &vertices, &plan,
        &compact_palette, &result) < 0 && errno == ENOSPC);
    assert(result.deformed_vertices == 0);
    pvr_skin_compact_palette_t bad = compact_palette;
    bad.version = 0;
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan, &bad, NULL) < 0);
    bad = compact_palette;
    --bad.joint_count;
    assert(pvr_skin_apply_spans_compact(small, 3, &vertices, &plan, &bad, NULL) < 0);
    assert(pvr_skin_apply_spans_compact((pvr_deform_vertex_t *)compact_joints,
        3, &vertices, &plan, &compact_palette, NULL) < 0 && errno == EINVAL);
    xmtrx_check();
}

static void test_compact_rounded_weight(void) {
    matrix_t positions[2] = {{{0}}};
    pvr_normal_matrix_t normals[2] = {{{{0}}}};
    for(size_t i = 0; i < 2; ++i) {
        for(size_t c = 0; c < 4; ++c)
            positions[i][c][c] = 1;
        for(size_t c = 0; c < 3; ++c)
            normals[i].column[c][c] = 1;
    }
    positions[0][0][0] = 2;
    pvr_skin_palette_t legacy = {positions, normals, 2};
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT,
                                           &imported_palette) == 0);
    pvr_skin_weight_t weights[] = {{0, 0, FLT_MIN}, {1, 0, FLT_MAX}};
    pvr_skin_span_t span = {0, 2, 0};
    pvr_skin_span_stream_t stream = {&span, 1, sizeof(span), weights, 2};
    pvr_skin_prepared_span_t stored_span;
    pvr_skin_weight_t stored_weights[2];
    pvr_skin_prepared_spans_t plan;
    assert(pvr_skin_spans_prepare(&stream, 2, &stored_span, 1, stored_weights,
                                  2, &plan) == 0);
    assert(plan.weight_count == 2 && stored_weights[0].weight == 0);
    pvr_deform_vertex_t source = {.position = {FLT_MAX, 0, 0, 1},
                                  .normal = {0, 0, 1, 0}};
    pvr_deform_stream_t vertices = {&source, 1, sizeof(source)};
    pvr_deform_vertex_t result_vertex;
    memset(&result_vertex, 0x5a, sizeof(result_vertex));
    const pvr_deform_vertex_t before_vertex = result_vertex;
    pvr_deform_result_t progress;
    /* Do not skip an originally positive weight just because normalization
       rounded it to zero: the corresponding transform still overflows. */
    assert(pvr_skin_apply_spans_compact(&result_vertex, 1, &vertices, &plan,
        &imported_palette, &progress) < 0 && errno == ERANGE);
    assert(progress.deformed_vertices == 0);
    assert(memcmp(&result_vertex, &before_vertex, sizeof(result_vertex)) == 0);
    xmtrx_check();
}

static void test_compact_failures(void) {
    init(0);
    prepare();
    compare();
    compare_compact();
    pvr_skin_compact_joint_t saved[COUNT];
    memcpy(saved, compact_import, sizeof(saved));
    pvr_skin_compact_palette_t saved_palette = imported_palette;
    pvr_skin_palette_t legacy = {reference, reference_normal, COUNT};
    reference[2][1][3] = 0.5f;
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT,
                                           &imported_palette) < 0 && errno == EDOM);
    assert(memcmp(saved, compact_import, sizeof(saved)) == 0);
    assert(memcmp(&saved_palette, &imported_palette, sizeof(saved_palette)) == 0);
    reference[2][1][3] = 0;
    reference_normal[2].column[1][1] = NAN;
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT,
                                           &imported_palette) < 0 && errno == EDOM);
    assert(memcmp(saved, compact_import, sizeof(saved)) == 0);
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT - 1,
                                           &imported_palette) < 0 && errno == ENOSPC);
    assert(pvr_skin_palette_prepare_compact(&legacy,
        (pvr_skin_compact_joint_t *)reference, COUNT, &imported_palette) < 0 && errno == EINVAL);
    assert(pvr_skin_palette_prepare_compact(&legacy, compact_import, COUNT,
        (pvr_skin_compact_palette_t *)compact_import) < 0 && errno == EINVAL);

    memcpy(saved, compact_joints, sizeof(saved));
    saved_palette = compact_palette;
    world[1][0][0] = 0; /* Late joint binds to this now-singular world matrix. */
    prepare();
    assert(pvr_chunk_skeleton_palette_build_compact(&affine, &pose,
        compact_joints, COUNT, &compact_palette) < 0 && errno == ERANGE);
    assert(memcmp(saved, compact_joints, sizeof(saved)) == 0);
    assert(memcmp(&saved_palette, &compact_palette, sizeof(saved_palette)) == 0);
    init(0);
    world[1][0][0] = FLT_MAX;
    joints[2].inverse_bind[0][0] = 4;
    prepare();
    assert(pvr_chunk_skeleton_palette_build_compact(&affine, &pose,
        compact_joints, COUNT, &compact_palette) < 0);
    assert(memcmp(saved, compact_joints, sizeof(saved)) == 0);
    assert(pvr_chunk_skeleton_palette_build_compact(&affine, &pose,
        compact_joints, COUNT - 1, &compact_palette) < 0 && errno == ENOSPC);
    assert(pvr_chunk_skeleton_palette_build_compact(&affine, &pose,
        compact_joints, COUNT, (pvr_skin_compact_palette_t *)compact_joints) < 0 && errno == EINVAL);
    xmtrx_check();
    test_compact_rounded_weight();
    printf("compact palette joint bytes=%u prepared=%u\n",
           (unsigned)sizeof(pvr_skin_compact_joint_t),
           (unsigned)sizeof(pvr_skin_prepared_joint_t));
}
