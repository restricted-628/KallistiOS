/* KallistiOS ##version##
   Shared host/SH-4 variable-span admission regression.
   Included after the independent oracle in skin-fixtures.h.
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_SKIN_SPAN_PLAN_FIXTURES_H
#define KOS_SKIN_SPAN_PLAN_FIXTURES_H

static const char *verify_skin_span_plan(const pvr_skin_palette_t *palette,
    const pvr_deform_vertex_t original[3], bool (*state_unchanged)(void),
    double tolerance) {
    struct { pvr_skin_span_t span; uint32_t guard[2]; } spans[3] = {
        { { 0, 6, 0 }, { 11, 12 } }, { { 1, 4, 0 }, { 21, 22 } },
        { { 0, 6, 0 }, { 31, 32 } }
    };
    pvr_skin_weight_t weights[7] = {
        { 2, 0, 2 }, { 0, 0, 3 }, { UINT16_MAX, 0, 0 }, { 3, 0, 1 },
        { 1, 0, 4 }, { 2, 0, 2 }, { UINT16_MAX, 1, NAN }
    };
    pvr_skin_span_stream_t stream = { spans, 3, sizeof(spans[0]), weights, 7 };
    pvr_skin_span_plan_requirements_t requirements;
    pvr_skin_prepared_span_t runs[4], saved_runs[4];
    pvr_skin_weight_t normalized[14], saved_weights[14];
    pvr_skin_prepared_spans_t plan, saved_plan;
    pvr_skin_prepared_joint_t joints[4];
    pvr_skin_prepared_palette_t pose;
    pvr_deform_vertex_t output[2][4], input[3];
    pvr_deform_stream_t vertices = { input, 3, sizeof(input[0]) };
    pvr_deform_result_t results[2];

    memset(runs, 0x5a, sizeof(runs));
    memset(normalized, 0x5a, sizeof(normalized));
    if(pvr_skin_palette_prepare(palette, joints, 4, &pose) ||
       pvr_skin_spans_prepare_query(&stream, 4, &requirements) ||
       requirements.span_count != 3 || requirements.weight_count != 13 ||
       pvr_skin_spans_prepare(&stream, 4, runs, 3, normalized, 13, &plan))
        return "span plan preparation/query";
    if(runs[0].first_weight != 0 || runs[0].weight_count != 5 ||
       runs[1].first_weight != 5 || runs[1].weight_count != 3 ||
       runs[2].first_weight != 8 || runs[2].weight_count != 5 ||
       normalized[1].weight == normalized[5].weight)
        return "span plan shared weights must normalize independently";
    memcpy(saved_runs, runs, sizeof(runs));
    memcpy(saved_weights, normalized, sizeof(normalized));
    memcpy(&saved_plan, &plan, sizeof(plan));
    for(size_t b = 0; b < sizeof(runs[3]); ++b)
        if(((const uint8_t *)(runs + 3))[b] != 0x5a)
            return "span plan run guard";
    for(size_t b = 0; b < sizeof(normalized[13]); ++b)
        if(((const uint8_t *)(normalized + 13))[b] != 0x5a)
            return "span plan weight guard";
    if(state_unchanged && !state_unchanged()) return "span preparation XMTRX";

    for(unsigned in_place = 0; in_place < 2; ++in_place)
    for(unsigned failure = 0; failure < 5; ++failure) {
        int status[2], errors[2];
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            memcpy(input, original, sizeof(input));
            memset(output[admitted], 0x5a, sizeof(output[admitted]));
            if(in_place) memcpy(output[admitted], original, sizeof(input));
            vertices.vertices = in_place ? output[admitted] : input;
            pvr_deform_vertex_t *changing = in_place ? output[admitted] : input;
            if(failure == 1) changing[1].position.x = NAN;
            if(failure == 2) memset(&changing[1].normal, 0, sizeof(changing[1].normal));
            if(failure == 4) vertices.stride = sizeof(input[0]) - 4;
            if(admitted) { weights[0].weight = NAN; spans[0].span.reserved = 1; }
            errno = 0;
            status[admitted] = admitted ?
                pvr_skin_apply_spans_prepared(output[admitted], failure == 3 ? 2 : 3,
                    &vertices, &plan, &pose, &results[admitted]) :
                pvr_skin_apply_spans_prepared_palette(output[admitted], failure == 3 ? 2 : 3,
                    &vertices, &stream, &pose, &results[admitted]);
            errors[admitted] = errno;
            weights[0].weight = 2;
            spans[0].span.reserved = 0;
            vertices.stride = sizeof(input[0]);
            if(state_unchanged && !state_unchanged()) return "span apply XMTRX";
        }
        const int expected[] = { 0, EDOM, ERANGE, ENOSPC, EINVAL };
        if(status[0] != (failure ? -1 : 0) || errors[0] != expected[failure] ||
           status[0] != status[1] || errors[0] != errors[1] ||
           results[0].deformed_vertices != results[1].deformed_vertices ||
           results[1].deformed_vertices != (failure == 0 ? 3u : failure < 3 ? 1u : 0u) ||
           memcmp(output[0], output[1], sizeof(output[0])))
            return "span plan checked/prepared equivalence";
        for(size_t b = 0; b < sizeof(output[1][3]); ++b)
            if(((const uint8_t *)&output[1][3])[b] != 0x5a)
                return "span plan output guard";
        if(!failure) for(size_t v = 0; v < 3; ++v) {
            double p[3], n[3];
            skin_reference(original + v, palette, weights + spans[v].span.first_weight,
                           spans[v].span.weight_count, p, n);
            if(!skin_matches(output[1] + v, p, n, tolerance))
                return "span plan independent scalar reference";
        }
        if(memcmp(runs, saved_runs, sizeof(runs)) ||
           memcmp(normalized, saved_weights, sizeof(normalized)))
            return "span plan storage mutated by application";
    }

    for(unsigned failure = 0; failure < 18; ++failure) {
        pvr_skin_span_t saved_span = spans[2].span;
        pvr_skin_weight_t originals[7];
        pvr_skin_span_stream_t trial = stream;
        memcpy(originals, weights, sizeof(weights));
        if(failure == 0) spans[2].span.reserved = 1;
        if(failure == 1) spans[2].span.weight_count = 0;
        if(failure == 2) spans[2].span.first_weight = 7;
        if(failure == 3) weights[0].weight = -1;
        if(failure == 4) weights[0].weight = NAN;
        if(failure == 5) weights[0].joint = 4;
        if(failure == 6) weights[0].reserved = 1;
        if(failure == 7) for(size_t s = 0; s < 6; ++s) weights[s].weight = 0;
        if(failure == 8) weights[0].weight = weights[1].weight = FLT_MAX;
        if(failure == 15) trial.stride = SIZE_MAX & ~(size_t)3;
        const size_t joint_count = failure == 16 ? 0 : 4;
        const pvr_skin_span_stream_t *source = failure == 17 ? NULL : &trial;
        requirements = (pvr_skin_span_plan_requirements_t){ 91, 92 };
        errno = 0;
        int query = pvr_skin_spans_prepare_query(source, joint_count, &requirements);
        int expected = failure < 9 ? EILSEQ : failure == 15 ? ERANGE : EINVAL;
        if(failure < 9 || failure >= 15) {
            if(query != -1 || errno != expected || requirements.span_count != 91 ||
               requirements.weight_count != 92)
                return "span plan rejected query publication";
        }
        else if(query || requirements.span_count != 3 || requirements.weight_count != 13)
            return "span plan successful query";
        errno = 0;
        int rc = pvr_skin_spans_prepare(source, joint_count,
            failure == 12 ? (void *)normalized :
            failure == 14 ? (void *)((uint8_t *)runs + 1) : runs,
            failure == 9 ? 2 : 3, failure == 11 ? weights : normalized,
            failure == 10 ? 12 : 13, failure == 13 ? (void *)runs : &plan);
        spans[2].span = saved_span;
        memcpy(weights, originals, sizeof(weights));
        if(failure == 9 || failure == 10) expected = ENOSPC;
        if(rc != -1 || errno != expected || memcmp(runs, saved_runs, sizeof(runs)) ||
           memcmp(normalized, saved_weights, sizeof(normalized)) ||
           memcmp(&plan, &saved_plan, sizeof(plan)))
            return "span plan preparation transaction";
        if(state_unchanged && !state_unchanged()) return "span rejected preparation XMTRX";
    }
    /* Query output cannot overwrite source arrays. */
    errno = 0;
    if(pvr_skin_spans_prepare_query(&stream, 4, (void *)spans) != -1 || errno != EINVAL)
        return "span query source overlap";

    memcpy(input, original, sizeof(input));
    vertices.vertices = input;
    for(unsigned failure = 0; failure < 9; ++failure) {
        pvr_skin_prepared_spans_t bad = plan;
        if(failure == 0) bad.version = 0;
        if(failure == 1) --bad.joint_count;
        if(failure == 2) --bad.vertex_count;
        if(failure == 7) bad.weight_count = SIZE_MAX;
        memset(output[0], 0x5a, sizeof(output[0]));
        memcpy(output[1], output[0], sizeof(output[0]));
        pvr_deform_vertex_t *destination = failure == 3 ? (void *)runs :
            failure == 4 ? (void *)normalized : failure == 5 ? (void *)&bad : output[0];
        errno = 0;
        int rc = pvr_skin_apply_spans_prepared(destination, 3, &vertices,
            failure == 6 ? NULL : &bad, failure == 8 ? NULL : &pose, results);
        if(rc != -1 || errno != (failure == 7 ? ERANGE : EINVAL) ||
           results[0].deformed_vertices || memcmp(runs, saved_runs, sizeof(runs)) ||
           memcmp(normalized, saved_weights, sizeof(normalized)) ||
           memcmp(output[0], output[1], sizeof(output[0])))
            return "span plan admission/overlap rejection";
        if(state_unchanged && !state_unchanged()) return "span rejected apply XMTRX";
    }

    /* An empty vertex stream has an empty plan, but still requires framing. */
    pvr_skin_span_stream_t empty = stream;
    empty.vertex_count = 0;
    vertices.vertex_count = 0;
    if(pvr_skin_spans_prepare_query(&empty, 4, &requirements) ||
       requirements.span_count || requirements.weight_count ||
       pvr_skin_spans_prepare(&empty, 4, runs, 0, normalized, 0, &plan) ||
       pvr_skin_apply_spans_prepared(output[0], 0, &vertices, &plan, &pose, results) ||
       results[0].deformed_vertices)
        return "empty span plan";

    /* Active source weights remain active after normalization underflows. */
    matrix_t extreme_positions[2] = { { { 0 } }, { { 0 } } };
    pvr_normal_matrix_t extreme_normals[2] = { { { { 0 } } }, { { { 0 } } } };
    for(size_t j = 0; j < 2; ++j) for(size_t a = 0; a < 3; ++a) {
        extreme_positions[j][a][a] = 1;
        extreme_normals[j].column[a][a] = 1;
    }
    extreme_positions[1][0][0] = FLT_MAX;
    pvr_skin_palette_t extreme = { extreme_positions, extreme_normals, 2 };
    pvr_skin_weight_t tiny[2] = { { 0, 0, FLT_MAX }, { 1, 0, FLT_MIN } };
    pvr_skin_span_t tiny_span = { 0, 2, 0 };
    pvr_skin_span_stream_t tiny_stream = { &tiny_span, 1, sizeof(tiny_span), tiny, 2 };
    pvr_deform_vertex_t point = { { 2, 0, 0, 1 }, { 0, 0, 1, 0 } };
    vertices = (pvr_deform_stream_t){ &point, 1, sizeof(point) };
    if(pvr_skin_palette_prepare(&extreme, joints, 2, &pose) ||
       pvr_skin_spans_prepare(&tiny_stream, 2, runs, 1, normalized, 2, &plan) ||
       plan.weight_count != 2 || normalized[1].weight != 0)
        return "span plan underflow preparation";
    for(unsigned admitted = 0; admitted < 2; ++admitted) {
        memset(output[admitted], 0x5a, sizeof(output[admitted]));
        errno = 0;
        int rc = admitted ?
            pvr_skin_apply_spans_prepared(output[admitted], 1, &vertices, &plan, &pose, results) :
            pvr_skin_apply_spans_prepared_palette(output[admitted], 1, &vertices,
                &tiny_stream, &pose, results);
        if(rc != -1 || errno != ERANGE || results[0].deformed_vertices)
            return "span plan underflow changed arithmetic failure";
        if(state_unchanged && !state_unchanged()) return "span underflow XMTRX";
    }
    if(memcmp(output[0], output[1], sizeof(output[0])))
        return "span plan underflow output";
    return NULL;
}
#endif
