/* KallistiOS ##version##
   Shared host/SH-4 skinning integration fixture.
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_SKIN_FIXTURES_H
#define KOS_SKIN_FIXTURES_H

#include <dc/pvr_deform.h>
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
        for(unsigned in_place = 0; in_place < 2; ++in_place) {
            memcpy(input, original, sizeof(input));
            memset(output, 0x5a, sizeof(output));
            memcpy(before, output, sizeof(output));
            pvr_deform_vertex_t *destination = in_place ? input : output;
            int rc = general ?
                pvr_skin_apply_spans(destination, 3, &vertices, &span_stream,
                                      &palette, &result) :
                pvr_skin_apply(destination, 3, &vertices, &fixed_stream,
                                &palette, &result);
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
            if(invalid_matrix)
                normals[0].column[1][2] = NAN;
            else {
                weights[0].joint = 4;
                fixed[0].joint[0] = 4;
            }
            errno = 0;
            int rc = general ?
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
    return NULL;
}
#endif
