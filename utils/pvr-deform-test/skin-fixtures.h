/* KallistiOS ##version##
   Shared host/SH-4 skinning integration fixture.
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_SKIN_FIXTURES_H
#define KOS_SKIN_FIXTURES_H

#include <dc/pvr_skin_prepared.h>
#include <stdbool.h>
#include <errno.h>
#include <math.h>
#include <string.h>

/* Independent double-precision oracle: no SH4ZAM or KOS math calls. */
static void skin_reference(const pvr_deform_vertex_t *source,
                           const pvr_skin_palette_t *palette,
                           const pvr_skin_weight_t *weights, size_t count,
                           double position[3], double normal[3]) {
    const double p[3] = { source->position.x, source->position.y,
                          source->position.z };
    const double n[3] = { source->normal.x, source->normal.y,
                          source->normal.z };
    double total = 0.0;
    double length = 0.0;

    for(size_t i = 0; i < count; ++i)
        total += weights[i].weight;
    for(size_t r = 0; r < 3; ++r) {
        position[r] = normal[r] = 0.0;
        for(size_t i = 0; i < count; ++i) {
            if(weights[i].weight == 0.0f)
                continue;
            size_t j = weights[i].joint;
            double tp = palette->position_matrices[j][3][r];
            double tn = 0.0;
            for(size_t c = 0; c < 3; ++c) {
                tp += palette->position_matrices[j][c][r] * p[c];
                tn += palette->normal_matrices[j].column[c][r] * n[c];
            }
            position[r] += tp * (weights[i].weight / total);
            normal[r] += tn * (weights[i].weight / total);
        }
        length += normal[r] * normal[r];
    }
    length = sqrt(length);
    for(size_t r = 0; r < 3; ++r)
        normal[r] /= length;
}

static bool skin_matches(const pvr_deform_vertex_t *actual,
                         const double position[3], const double normal[3],
                         double tolerance) {
    const double p[3] = { actual->position.x, actual->position.y,
                          actual->position.z };
    const double n[3] = { actual->normal.x, actual->normal.y,
                          actual->normal.z };
    for(size_t r = 0; r < 3; ++r)
        if(!isfinite(p[r]) || !isfinite(n[r]) ||
           fabs(p[r] - position[r]) > tolerance ||
           fabs(n[r] - normal[r]) > tolerance)
            return false;
    return actual->position.w == 1.0f && actual->normal.w == 0.0f;
}

static const char *verify_skin_matrices(bool (*state_unchanged)(void),
                                        double tolerance) {
    matrix_t positions[4];
    pvr_normal_matrix_t normals[4];
    pvr_skin_palette_t palette = { positions, normals, 4 };
    const pvr_deform_vertex_t original[3] = {
        { { 1.25f, -2.0f, 0.75f, 99.0f }, { 0.4f, 0.7f, -0.2f, 99.0f } },
        { { -3.0f, 0.5f, 2.0f, -7.0f }, { -0.3f, 0.2f, 0.9f, -7.0f } },
        { { 0.0f, -1.5f, 4.0f, 0.0f }, { 0.6f, -0.4f, 0.5f, 1.0f } }
    };
    pvr_skin_weight_t weights[6] = {
        { 2, 0, 2.0f }, { 0, 0, 3.0f }, { 3, 0, 1.0f },
        { UINT16_MAX, 0, 0.0f }, { 1, 0, 4.0f }, { 2, 0, 2.0f }
    };
    pvr_skin_influences_t fixed[3];
    pvr_skin_span_t spans[3] = { { 0, 6, 0 }, { 0, 6, 0 }, { 0, 6, 0 } };
    pvr_skin_stream_t fixed_stream = { fixed, 3, sizeof(fixed[0]) };
    pvr_skin_span_stream_t span_stream = {
        spans, 3, sizeof(spans[0]), weights, 6
    };
    pvr_deform_vertex_t input[3], output[4], before[4];
    pvr_deform_stream_t vertices = { input, 3, sizeof(input[0]) };
    pvr_deform_result_t result;
    pvr_skin_prepared_joint_t imported[5], import_before[5];
    pvr_skin_prepared_palette_t prepared;

    /* Distinct nonsymmetric columns expose transpose/layout mistakes.
       Normal matrices are intentionally independent from position matrices. */
    memset(positions, 0, sizeof(positions));
    for(size_t j = 0; j < 4; ++j) {
        for(size_t c = 0; c < 3; ++c) {
            for(size_t r = 0; r < 3; ++r) {
                positions[j][c][r] = c == r ? 1.0f + 0.25f * j :
                    0.0625f * (float)(1 + j + 2 * c + r);
                normals[j].column[c][r] = c == r ? 0.75f + 0.125f * j :
                    -0.03125f * (float)(1 + 2 * j + c + 3 * r);
            }
            positions[j][3][c] = (float)(j + 1) * (float)((int)c - 1);
        }
        positions[j][3][3] = 1.0f;
    }
    for(size_t v = 0; v < 3; ++v)
        for(size_t i = 0; i < 4; ++i) {
            fixed[v].joint[i] = weights[i].joint;
            fixed[v].weight[i] = weights[i].weight;
        }

    for(unsigned general = 0; general < 2; ++general) {
      for(unsigned admitted = 0; admitted < 2; ++admitted) {
        for(unsigned in_place = 0; in_place < 2; ++in_place) {
            memcpy(input, original, sizeof(input));
            memset(output, 0x5a, sizeof(output));
            memcpy(before, output, sizeof(output));
            pvr_deform_vertex_t *destination = in_place ? input : output;
            memset(imported, 0x5a, sizeof(imported));
            memcpy(import_before, imported, sizeof(imported));
            if(pvr_skin_palette_prepare(&palette, imported, 4, &prepared))
                return "skin palette preparation";
            if(memcmp(imported + 4, import_before + 4, sizeof(imported[4])))
                return "skin palette storage guard";
            if(state_unchanged && !state_unchanged())
                return "skin palette preparation XMTRX";
            /* A prepared pose is a copy, not a borrowed KOS matrix view. */
            float old_position = positions[0][0][0];
            float old_normal = normals[0].column[0][0];
            if(admitted) {
                positions[0][0][0] = NAN;
                normals[0].column[0][0] = NAN;
            }
            int rc = admitted ? (general ?
                pvr_skin_apply_spans_prepared_palette(destination, 3, &vertices,
                    &span_stream, &prepared, &result) :
                pvr_skin_apply_prepared_palette(destination, 3, &vertices,
                    &fixed_stream, &prepared, &result)) : general ?
                pvr_skin_apply_spans(destination, 3, &vertices, &span_stream,
                                      &palette, &result) :
                pvr_skin_apply(destination, 3, &vertices, &fixed_stream,
                                &palette, &result);
            positions[0][0][0] = old_position;
            normals[0].column[0][0] = old_normal;
            if(rc || result.deformed_vertices != 3)
                return "skin blend execution";
            if(state_unchanged && !state_unchanged())
                return "skin blend XMTRX preservation";
            for(size_t v = 0; v < 3; ++v) {
                double p[3], n[3];
                skin_reference(original + v, &palette, weights,
                               general ? 6 : 4, p, n);
                if(!skin_matches(destination + v, p, n, tolerance))
                    return "skin blend scalar reference";
            }
            if(memcmp(output + 3, before + 3, sizeof(output[3])))
                return "skin output guard";
        }
        /* Invalid active indices and nonfinite matrices reject publication. */
        for(unsigned invalid_matrix = 0; invalid_matrix < 2; ++invalid_matrix) {
            memcpy(input, original, sizeof(input));
            memset(output, 0x5a, sizeof(output));
            memcpy(before, output, sizeof(output));
            if(pvr_skin_palette_prepare(&palette, imported, 4, &prepared))
                return "skin rejection palette preparation";
            if(invalid_matrix)
                normals[0].column[1][2] = NAN;
            else {
                weights[0].joint = 4;
                fixed[0].joint[0] = 4;
            }
            errno = 0;
            /* Nonfinite palette values are rejected at preparation, not
               rescanned by an application of an older immutable snapshot. */
            result.deformed_vertices = 0;
            int rc = admitted ? (invalid_matrix ?
                pvr_skin_palette_prepare(&palette, imported, 4, &prepared) :
                general ?
                pvr_skin_apply_spans_prepared_palette(output, 3, &vertices,
                    &span_stream, &prepared, &result) :
                pvr_skin_apply_prepared_palette(output, 3, &vertices,
                    &fixed_stream, &prepared, &result)) : general ?
                pvr_skin_apply_spans(output, 3, &vertices, &span_stream,
                                      &palette, &result) :
                pvr_skin_apply(output, 3, &vertices, &fixed_stream,
                                &palette, &result);
            if(rc != -1 || errno != (invalid_matrix ? EDOM : EILSEQ) ||
               result.deformed_vertices || memcmp(output, before, sizeof(output)))
                return "skin rejected input publication";
            if(state_unchanged && !state_unchanged())
                return "skin rejected input XMTRX";
            weights[0].joint = fixed[0].joint[0] = 2;
            normals[0].column[1][2] = -0.25f;
        }
      }
    }

    /* Both APIs preserve the same valid prefix for changing vertex failures;
       weights remain mutable and are still validated before publication. */
    for(unsigned general = 0; general < 2; ++general)
    for(unsigned failure = 0; failure < 7; ++failure) {
        pvr_deform_vertex_t checked[4];
        pvr_deform_result_t results[2];
        int status[2], errors[2];
        if(pvr_skin_palette_prepare(&palette, imported, 4, &prepared))
            return "skin dynamic palette preparation";
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            memcpy(input, original, sizeof(input));
            memset(output, 0x5a, sizeof(output));
            memcpy(before, output, sizeof(output));
            if(failure == 0) input[1].position.x = NAN;
            if(failure == 1) memset(&input[1].normal, 0, sizeof(input[1].normal));
            if(failure == 2) weights[0].weight = fixed[0].weight[0] = -1;
            if(failure == 3) weights[0].weight = fixed[0].weight[0] = NAN;
            if(failure == 4) vertices.stride = sizeof(input[0]) - 4;
            size_t capacity = failure == 5 ? 2 : 3;
            errno = 0;
            status[admitted] = admitted ? (general ?
                pvr_skin_apply_spans_prepared_palette(output, capacity, &vertices,
                    &span_stream, &prepared, &results[admitted]) :
                pvr_skin_apply_prepared_palette(output, capacity, &vertices,
                    &fixed_stream, &prepared, &results[admitted])) : general ?
                pvr_skin_apply_spans(output, capacity, &vertices, &span_stream,
                    &palette, &results[admitted]) :
                pvr_skin_apply(output, capacity, &vertices, &fixed_stream,
                    &palette, &results[admitted]);
            errors[admitted] = errno;
            if(!admitted) memcpy(checked, output, sizeof(output));
            else if(memcmp(checked, output, sizeof(output)))
                return "skin prepared output equivalence";
            if(state_unchanged && !state_unchanged())
                return "skin dynamic XMTRX";
            size_t prefix = results[admitted].deformed_vertices;
            if(prefix > 3) return "skin invalid prefix count";
            if(memcmp(output + prefix, before + prefix,
                      (4 - prefix) * sizeof(output[0])))
                return "skin dynamic output tail";
            weights[0].weight = fixed[0].weight[0] = 2;
            vertices.stride = sizeof(input[0]);
        }
        const int expected[] = { EDOM, ERANGE, EILSEQ, EILSEQ, EINVAL, ENOSPC, 0 };
        if(status[0] != (failure == 6 ? 0 : -1) ||
           errors[0] != expected[failure] || status[0] != status[1] ||
           errors[0] != errors[1] || results[0].deformed_vertices !=
           results[1].deformed_vertices || results[0].deformed_vertices !=
           (failure < 2 ? 1u : failure == 6 ? 3u : 0u))
            return "skin prepared dynamic result equivalence";
    }

    /* Preparation must be transactional, even when a late joint is invalid. */
    for(unsigned failure = 0; failure < 7; ++failure) {
        pvr_skin_prepared_palette_t saved;
        memset(imported, 0x5a, sizeof(imported));
        memcpy(import_before, imported, sizeof(imported));
        memset(&prepared, 0x5a, sizeof(prepared));
        memcpy(&saved, &prepared, sizeof(saved));
        float old_position = positions[3][2][1];
        float old_normal = normals[3].column[2][1];
        if(failure == 0) positions[3][2][1] = NAN;
        if(failure == 1) normals[3].column[2][1] = NAN;
        errno = 0;
        int rc = pvr_skin_palette_prepare(failure == 6 ? NULL : &palette,
            failure == 3 ? (void *)positions : imported,
            failure == 2 ? 3 : 4,
            failure == 4 ? (void *)imported :
            failure == 5 ? (void *)((uint8_t *)&prepared + 1) : &prepared);
        positions[3][2][1] = old_position;
        normals[3].column[2][1] = old_normal;
        if(rc != -1 || errno != (failure < 2 ? EDOM :
                                 failure == 2 ? ENOSPC : EINVAL) ||
           memcmp(imported, import_before, sizeof(imported)) ||
           memcmp(&prepared, &saved, sizeof(saved)))
            return "skin palette preparation transaction";
        if(state_unchanged && !state_unchanged())
            return "skin palette rejected preparation XMTRX";
    }
    if(pvr_skin_palette_prepare(&palette, imported, 4, &prepared))
        return "skin overlap palette preparation";
    memcpy(import_before, imported, sizeof(imported));
    memcpy(input, original, sizeof(input));
    for(unsigned general = 0; general < 2; ++general)
    for(unsigned failure = 0; failure < 4; ++failure) {
        pvr_skin_prepared_palette_t bad = prepared;
        if(failure == 0) bad.version = 0;
        pvr_deform_vertex_t *destination = failure == 1 ? (void *)imported :
            failure == 2 ? (void *)&bad : output;
        errno = 0;
        int rc = general ?
            pvr_skin_apply_spans_prepared_palette(destination, 3, &vertices,
                &span_stream, failure == 3 ? NULL : &bad, &result) :
            pvr_skin_apply_prepared_palette(destination, 3, &vertices,
                &fixed_stream, failure == 3 ? NULL : &bad, &result);
        if(rc != -1 || errno != EINVAL || result.deformed_vertices ||
           memcmp(imported, import_before, sizeof(imported)))
            return "skin prepared admission or overlap rejection";
        if(state_unchanged && !state_unchanged())
            return "skin prepared rejection XMTRX";
    }
    return NULL;
}
#endif
