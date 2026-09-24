/* KallistiOS ##version##
   Synthetic skinning workloads: correctness on host, timing on SH-4 only.
   Copyright (C) 2026 Joseph Black
*/
#ifdef __DREAMCAST__
#include <kos.h>
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);
#endif
#include <dc/pvr_skin_prepared.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_VERTICES 256u
#define MAX_MESHES 4u
#define JOINTS 16u
#define REPEATS 5u
#define FRAMES 2u
#define SETUP_ITERATIONS 8u
enum { WEIGHT_SETUP, PALETTE_SETUP, COMPACT_PALETTE_SETUP,
       APPLY_ONLY, POSE_AND_APPLY, MODE_COUNT };
enum { CHECKED, PREPARED_PALETTE, PREPARED_WEIGHTS, COMPACT_WEIGHTS, LANE_COUNT };
#ifdef __DREAMCAST__
static const char *const mode_names[] = {
    "weight-setup", "palette-setup", "compact-palette-setup", "apply", "pose+apply" };
static const char *const lane_names[] = {
    "checked", "palette", "palette+weights", "compact+weights" };
#endif
static const char *const shape_names[] = { "fixed1", "fixed4", "span1-8" };
static size_t vertex_count, mesh_count;
static unsigned shape;
static matrix_t positions[2][JOINTS];
static pvr_normal_matrix_t normals[2][JOINTS];
static pvr_skin_palette_t palettes[2];
static pvr_skin_prepared_joint_t joint_storage[JOINTS];
static pvr_skin_prepared_palette_t prepared_palette;
static pvr_skin_compact_joint_t compact_storage[JOINTS];
static pvr_skin_compact_palette_t compact_palette;
static pvr_deform_vertex_t input[MAX_MESHES][MAX_VERTICES];
static pvr_deform_vertex_t output[MAX_MESHES][MAX_VERTICES + 1];
static pvr_skin_influences_t fixed[MAX_MESHES][MAX_VERTICES];
static pvr_skin_span_t spans[MAX_MESHES][MAX_VERTICES];
static pvr_skin_weight_t weights[MAX_MESHES][MAX_VERTICES * 8];
static pvr_skin_prepared_influence_t fixed_storage[MAX_MESHES][MAX_VERTICES];
static pvr_skin_prepared_influences_t fixed_plan[MAX_MESHES];
static pvr_skin_prepared_span_t span_storage[MAX_MESHES][MAX_VERTICES];
static pvr_skin_weight_t weight_storage[MAX_MESHES][MAX_VERTICES * 8];
static pvr_skin_prepared_spans_t span_plan[MAX_MESHES];
static pvr_deform_stream_t vertices[MAX_MESHES];
static pvr_skin_stream_t fixed_stream[MAX_MESHES];
static pvr_skin_span_stream_t span_stream[MAX_MESHES];
static struct { double p[3], n[3]; } expected[2][MAX_MESHES][MAX_VERTICES];
#ifdef __DREAMCAST__
static shz_mat4x4_t sentinel;
static uint32_t fp_modes;
#endif

static bool state_ok(void) {
#ifdef __DREAMCAST__
    shz_mat4x4_t observed;
    shz_xmtrx_store_4x4(&observed);
    return !memcmp(&sentinel, &observed, sizeof(observed)) &&
           (__builtin_sh_get_fpscr() & ((15u << 18) | 3u)) == fp_modes;
#else
    return true; /* Accelerator/control state is checked on SH-4 only. */
#endif
}

static void init_palettes(void) {
    for(unsigned pose = 0; pose < 2; ++pose) {
        palettes[pose] = (pvr_skin_palette_t){ positions[pose], normals[pose], JOINTS };
        for(unsigned j = 0; j < JOINTS; ++j) {
            float angle = (float)(j + 1) * 0.03125f + pose * 0.125f;
            float c = cosf(angle), s = sinf(angle);
            float sx = 1.0f + (j % 3) * 0.125f, sy = 0.75f, sz = 1.25f;
            positions[pose][j][0][0] = c * sx;
            positions[pose][j][0][1] = s * sx;
            positions[pose][j][1][0] = -s * sy;
            positions[pose][j][1][1] = c * sy;
            positions[pose][j][2][2] = sz;
            positions[pose][j][3][0] = j * 0.0625f;
            positions[pose][j][3][1] = pose * 0.25f;
            positions[pose][j][3][3] = 1;
            normals[pose][j].column[0][0] = c / sx;
            normals[pose][j].column[0][1] = s / sx;
            normals[pose][j].column[1][0] = -s / sy;
            normals[pose][j].column[1][1] = c / sy;
            normals[pose][j].column[2][2] = 1.0f / sz;
        }
    }
}

/* Independent scalar double oracle; no KOS or SH4ZAM arithmetic helpers. */
static void reference(unsigned pose, size_t mesh, size_t v) {
    const double p[] = { input[mesh][v].position.x, input[mesh][v].position.y,
                         input[mesh][v].position.z };
    const double n[] = { input[mesh][v].normal.x, input[mesh][v].normal.y,
                         input[mesh][v].normal.z };
    const pvr_skin_weight_t *w = weights[mesh] + v * 8;
    size_t count = spans[mesh][v].weight_count;
    double total = 0, length = 0;
    for(size_t i = 0; i < count; ++i) total += w[i].weight;
    for(size_t r = 0; r < 3; ++r) {
        double position = 0, normal = 0;
        for(size_t i = 0; i < count; ++i) {
            size_t j = w[i].joint;
            double tp = positions[pose][j][3][r], tn = 0;
            for(size_t c = 0; c < 3; ++c) {
                tp += positions[pose][j][c][r] * p[c];
                tn += normals[pose][j].column[c][r] * n[c];
            }
            position += tp * (w[i].weight / total);
            normal += tn * (w[i].weight / total);
        }
        expected[pose][mesh][v].p[r] = position;
        expected[pose][mesh][v].n[r] = normal;
        length += normal * normal;
    }
    length = sqrt(length);
    for(size_t r = 0; r < 3; ++r) expected[pose][mesh][v].n[r] /= length;
}

static void init_meshes(void) {
    memset(fixed, 0, sizeof(fixed));
    memset(weights, 0, sizeof(weights));
    for(size_t m = 0; m < mesh_count; ++m) {
        vertices[m] = (pvr_deform_stream_t){ input[m], vertex_count, sizeof(input[m][0]) };
        fixed_stream[m] = (pvr_skin_stream_t){ fixed[m], vertex_count, sizeof(fixed[m][0]) };
        span_stream[m] = (pvr_skin_span_stream_t){ spans[m], vertex_count,
            sizeof(spans[m][0]), weights[m], vertex_count * 8 };
        for(size_t v = 0; v < vertex_count; ++v) {
            input[m][v] = (pvr_deform_vertex_t){
                { (float)(v % 16) * 0.125f, (float)(v / 16) * 0.125f,
                  (float)m * 0.25f, 1 }, { 0.25f, 0.5f + m * 0.0625f, 1, 0 }
            };
            size_t count = shape == 0 ? 1 : shape == 1 ? 4 : 1 + v % 8;
            spans[m][v] = (pvr_skin_span_t){ (uint32_t)(v * 8), (uint16_t)count, 0 };
            for(size_t i = 0; i < count; ++i) {
                uint16_t joint = (uint16_t)((v * 3 + i * 5 + m) % JOINTS);
                float weight = (float)(1 + (v + i) % 5);
                weights[m][v * 8 + i] = (pvr_skin_weight_t){ joint, 0, weight };
                if(shape < 2) {
                    fixed[m][v].joint[i] = joint;
                    fixed[m][v].weight[i] = weight;
                }
            }
            for(unsigned pose = 0; pose < 2; ++pose) reference(pose, m, v);
        }
    }
}

static int prepare_weights(void) {
    for(size_t m = 0; m < mesh_count; ++m) {
        if(shape < 2) {
            if(pvr_skin_influences_prepare(&fixed_stream[m], JOINTS,
                    fixed_storage[m], vertex_count, &fixed_plan[m])) return -1;
        }
        else {
            pvr_skin_span_plan_requirements_t need;
            if(pvr_skin_spans_prepare_query(&span_stream[m], JOINTS, &need) ||
               need.span_count > MAX_VERTICES || need.weight_count > MAX_VERTICES * 8 ||
               pvr_skin_spans_prepare(&span_stream[m], JOINTS, span_storage[m],
                   need.span_count, weight_storage[m], need.weight_count, &span_plan[m])) return -1;
        }
    }
    return 0;
}

static int prepare_palette(unsigned pose, unsigned lane) {
    if(lane == COMPACT_WEIGHTS)
        return pvr_skin_palette_prepare_compact(&palettes[pose], compact_storage,
                                                JOINTS, &compact_palette);
    return pvr_skin_palette_prepare(&palettes[pose], joint_storage, JOINTS, &prepared_palette);
}

static int apply(unsigned lane, unsigned pose, size_t m) {
    pvr_deform_result_t result;
    int rc;
    if(shape < 2) {
        if(lane == 0) rc = pvr_skin_apply(output[m], vertex_count, &vertices[m],
            &fixed_stream[m], &palettes[pose], &result);
        else if(lane == 1) rc = pvr_skin_apply_prepared_palette(output[m], vertex_count,
            &vertices[m], &fixed_stream[m], &prepared_palette, &result);
        else if(lane == COMPACT_WEIGHTS) rc = pvr_skin_apply_compact(output[m],
            vertex_count, &vertices[m], &fixed_plan[m], &compact_palette, &result);
        else rc = pvr_skin_apply_prepared(output[m], vertex_count, &vertices[m],
            &fixed_plan[m], &prepared_palette, &result);
    }
    else {
        if(lane == 0) rc = pvr_skin_apply_spans(output[m], vertex_count, &vertices[m],
            &span_stream[m], &palettes[pose], &result);
        else if(lane == 1) rc = pvr_skin_apply_spans_prepared_palette(output[m], vertex_count,
            &vertices[m], &span_stream[m], &prepared_palette, &result);
        else if(lane == COMPACT_WEIGHTS) rc = pvr_skin_apply_spans_compact(output[m],
            vertex_count, &vertices[m], &span_plan[m], &compact_palette, &result);
        else rc = pvr_skin_apply_spans_prepared(output[m], vertex_count, &vertices[m],
            &span_plan[m], &prepared_palette, &result);
    }
    return rc || result.deformed_vertices != vertex_count ? -1 : 0;
}

static bool output_ok(unsigned pose) {
#ifdef __DREAMCAST__
    const double tolerance = 0.0003;
#else
    const double tolerance = 0.00002;
#endif
    for(size_t m = 0; m < mesh_count; ++m) {
        for(size_t v = 0; v < vertex_count; ++v) {
            const pvr_deform_vertex_t *a = &output[m][v];
            const double p[] = { a->position.x, a->position.y, a->position.z };
            const double n[] = { a->normal.x, a->normal.y, a->normal.z };
            if(a->position.w != 1 || a->normal.w != 0) return false;
            for(size_t r = 0; r < 3; ++r)
                if(!isfinite(p[r]) || !isfinite(n[r]) ||
                   fabs(p[r] - expected[pose][m][v].p[r]) > tolerance ||
                   fabs(n[r] - expected[pose][m][v].n[r]) > tolerance) return false;
        }
        const uint8_t *tail = (const uint8_t *)(output[m] + vertex_count);
        for(size_t b = 0; b < (MAX_VERTICES + 1 - vertex_count) * sizeof(output[m][0]); ++b)
            if(tail[b] != 0x5a) return false;
    }
    return state_ok();
}

static int run(unsigned mode, unsigned lane, unsigned iterations) {
    for(unsigned f = 0; f < iterations; ++f) {
        if(mode == WEIGHT_SETUP) { if(prepare_weights()) return -1; }
        else if(mode == PALETTE_SETUP) {
            if(prepare_palette(f & 1u, PREPARED_WEIGHTS)) return -1;
        }
        else if(mode == COMPACT_PALETTE_SETUP) {
            if(prepare_palette(f & 1u, COMPACT_WEIGHTS)) return -1;
        }
        else {
            unsigned pose = mode == POSE_AND_APPLY ? f & 1u : 0;
            if(lane && mode == POSE_AND_APPLY && prepare_palette(pose, lane)) return -1;
            for(size_t m = 0; m < mesh_count; ++m) if(apply(lane, pose, m)) return -1;
        }
    }
    return 0;
}

static int correctness(void) {
    if(prepare_weights() || !state_ok()) return -1;
    for(unsigned pose = 0; pose < 2; ++pose) {
        for(unsigned lane = 0; lane < LANE_COUNT; ++lane) {
            if(prepare_palette(pose, lane) || !state_ok()) return -1;
            memset(output, 0x5a, sizeof(output));
            for(size_t m = 0; m < mesh_count; ++m) if(apply(lane, pose, m)) return -1;
            if(!output_ok(pose)) return -1;
        }
    }
    /* Exercise the exact scheduling used by the timed loops on host too. */
    for(unsigned mode = 0; mode < MODE_COUNT; ++mode)
    for(unsigned lane = 0; lane < LANE_COUNT; ++lane) {
        if(prepare_palette(0, lane)) return -1;
        memset(output, 0x5a, sizeof(output));
        if(run(mode, lane, FRAMES) || !state_ok()) return -1;
        if(mode >= APPLY_ONLY && !output_ok(mode == POSE_AND_APPLY ? 1 : 0)) return -1;
        if(mode < APPLY_ONLY) {
            unsigned pose = mode == WEIGHT_SETUP ? 0 : (FRAMES - 1) & 1u;
            unsigned consumer = mode == COMPACT_PALETTE_SETUP ? COMPACT_WEIGHTS :
                mode == PALETTE_SETUP ? PREPARED_WEIGHTS :
                lane == COMPACT_WEIGHTS ? COMPACT_WEIGHTS : PREPARED_WEIGHTS;
            for(size_t m = 0; m < mesh_count; ++m) if(apply(consumer, pose, m)) return -1;
            if(!output_ok(pose)) return -1;
        }
    }
    return 0;
}

#ifdef __DREAMCAST__
static int timings(void) {
    for(unsigned mode = 0; mode < MODE_COUNT; ++mode) {
        uint64_t times[LANE_COUNT][REPEATS];
        unsigned lanes = mode < APPLY_ONLY ? 1 : LANE_COUNT;
        unsigned iterations = mode < APPLY_ONLY ? SETUP_ITERATIONS : FRAMES;
        for(unsigned lane = 0; lane < lanes; ++lane) {
            if(prepare_palette(0, lane) || run(mode, lane, iterations) || !state_ok()) return -1;
        }
        for(unsigned trial = 0; trial < REPEATS; ++trial)
        for(unsigned order = 0; order < lanes; ++order) {
            unsigned lane = (trial + order) % lanes;
            if(prepare_palette(0, lane)) return -1;
            memset(output, 0x5a, sizeof(output));
            uint64_t start = timer_us_gettime64();
            int rc = run(mode, lane, iterations);
            uint64_t elapsed = timer_us_gettime64() - start;
            times[lane][trial] = elapsed;
            if(rc || !state_ok()) return -1;
            if(mode >= APPLY_ONLY) {
                if(!output_ok(mode == POSE_AND_APPLY ? (iterations - 1) & 1u : 0)) return -1;
            }
            else {
                /* Consume timed setup results outside timing; detect bad plans
                   or palettes with a real apply and the independent oracle. */
                unsigned pose = mode == WEIGHT_SETUP ? 0 : (iterations - 1) & 1u;
                unsigned consumer = mode == COMPACT_PALETTE_SETUP ? COMPACT_WEIGHTS : PREPARED_WEIGHTS;
                for(size_t m = 0; m < mesh_count; ++m) if(apply(consumer, pose, m)) return -1;
                if(!output_ok(pose)) return -1;
            }
            printf("SKIN sample shape=%s vertices=%u meshes=%u mode=%s lane=%s trial=%u us=%" PRIu64 "\n",
                shape_names[shape], (unsigned)vertex_count, (unsigned)mesh_count,
                mode_names[mode], lanes == 1 ? "setup" : lane_names[lane], trial, elapsed);
        }
        for(unsigned lane = 0; lane < lanes; ++lane) {
            for(size_t i = 1; i < REPEATS; ++i) {
                uint64_t value = times[lane][i];
                size_t j = i;
                while(j && times[lane][j - 1] > value) { times[lane][j] = times[lane][j - 1]; --j; }
                times[lane][j] = value;
            }
            printf("SKIN summary shape=%s vertices=%u meshes=%u mode=%s lane=%s iterations=%u min_us=%" PRIu64
                   " median_us=%" PRIu64 " max_us=%" PRIu64 "\n", shape_names[shape],
                (unsigned)vertex_count, (unsigned)mesh_count, mode_names[mode],
                lanes == 1 ? "setup" : lane_names[lane], iterations, times[lane][0],
                times[lane][REPEATS / 2], times[lane][REPEATS - 1]);
        }
    }
    return 0;
}
#endif

int main(void) {
    int rc = 0;
#ifdef __DREAMCAST__
    shz_mat4x4_t original;
    shz_xmtrx_store_4x4(&original);
    for(size_t i = 0; i < 16; ++i) sentinel.elem[i] = (float)(i + 1) * 0.25f;
    shz_xmtrx_load_4x4(&sentinel);
    fp_modes = __builtin_sh_get_fpscr() & ((15u << 18) | 3u);
    printf("SKIN compiler=%s repeats=%u frames=%u setup_iterations=%u interrupts=enabled\n",
           __VERSION__, REPEATS, FRAMES, SETUP_ITERATIONS);
    puts("SKIN timings require physical hardware; emulator timings are not speed evidence.");
#endif
    init_palettes();
    for(shape = 0; !rc && shape < 3; ++shape)
    for(vertex_count = 64; !rc && vertex_count <= MAX_VERTICES; vertex_count *= 4)
    for(mesh_count = 1; !rc && mesh_count <= MAX_MESHES; mesh_count *= 4) {
        init_meshes();
        rc = correctness();
        size_t weight_bytes = 0;
        for(size_t m = 0; m < mesh_count; ++m)
            weight_bytes += shape < 2 ? vertex_count * sizeof(fixed_storage[0][0]) :
                vertex_count * sizeof(span_storage[0][0]) + span_plan[m].weight_count * sizeof(weight_storage[0][0]);
        printf("SKIN correctness shape=%s vertices=%u meshes=%u weights_bytes=%u palette_bytes=%u compact_palette_bytes=%u result=%s\n",
            shape_names[shape], (unsigned)vertex_count, (unsigned)mesh_count,
            (unsigned)weight_bytes, (unsigned)sizeof(joint_storage),
            (unsigned)sizeof(compact_storage), rc ? "FAIL" : "PASS");
#ifdef __DREAMCAST__
        if(!rc) rc = timings();
#endif
    }
#ifdef __DREAMCAST__
    shz_xmtrx_load_4x4(&original);
#endif
    printf("RESULT: %s (skinning workload benchmark; production unchanged)\n", rc ? "FAIL" : "PASS");
    return rc ? 1 : 0;
}
