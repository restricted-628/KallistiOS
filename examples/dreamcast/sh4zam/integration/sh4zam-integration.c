/* KallistiOS ##version##

   SH4ZAM/KOS integration example
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/sh4zam.h>
#include <dc/pvr_chunk_cache.h>

#include <dc/biosfont.h>
#include <dc/video.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "matrix-fixtures.h"
#include "toon-draw-fixtures.h"

static shz_mat4x4_t matrix_sentinel;

static bool matrix_state_unchanged(void) {
    shz_mat4x4_t observed;

    shz_xmtrx_store_4x4(&observed);
    return !memcmp(&observed, &matrix_sentinel, sizeof(observed));
}

/* Use SCIF so emulator and hardware validation can capture a deterministic
   result without depending on a loader-provided debug console. */
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

#define CHUNK_VERTEX_HEADER(type, size) \
    ((uint32_t)(type) | ((uint32_t)(size) << 16))

static const uint32_t compact_vertices[] = {
    CHUNK_VERTEX_HEADER(PVR_CHUNK_VERTEX_XYZ, 10),
    UINT32_C(0x00030000),
    UINT32_C(0xbf800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0x3f800000), UINT32_C(0xbf800000), UINT32_C(0),
    UINT32_C(0), UINT32_C(0x3f800000), UINT32_C(0),
    UINT32_C(0x000000ff)
};

static const uint16_t compact_polygons[] = {
    PVR_CHUNK_MATERIAL_DIFFUSE, UINT16_C(2),
    UINT16_C(0x8040), UINT16_C(0xff20),
    PVR_CHUNK_STRIP_INDEX, UINT16_C(5), UINT16_C(1),
    UINT16_C(3), UINT16_C(0), UINT16_C(1), UINT16_C(2),
    UINT16_C(0x00ff)
};

static uint8_t fiber_stack[2048] __attribute__((aligned(THD_STACK_ALIGNMENT)));
static const pvr_vertex_t geometry_input[2] __attribute__((aligned(32))) = {
    {
        .flags = PVR_CMD_VERTEX,
        .x = 2.0f, .y = 3.0f, .z = 4.0f,
        .argb = UINT32_C(0xff102030)
    },
    {
        .flags = PVR_CMD_VERTEX_EOL,
        .x = -2.0f, .y = -3.0f, .z = -4.0f,
        .argb = UINT32_C(0xff405060)
    }
};
static kfiber_t *main_fiber;
static shz_mat4x4_t fiber_matrix;
static int fiber_result;

typedef struct fpscr_probe {
    uint32_t expected;
    bool passed;
} fpscr_probe_t;

static void *fpscr_thread(void *data) {
    fpscr_probe_t *probe = data;
    const uint32_t modes = (1u << 18) | 3u; /* DN and rounding mode. */
    uint32_t observed = __builtin_sh_get_fpscr();
    probe->passed = (observed & modes) == (probe->expected & modes);
    /* Child changes must survive scheduling without leaking to the creator.
       Leave PR/SZ/FR and exception enables alone throughout this fixture. */
    uint32_t changed = observed ^ 1u;
    __builtin_sh_set_fpscr(changed);
    thd_pass();
    probe->passed &= (__builtin_sh_get_fpscr() & modes) == (changed & modes);
    __builtin_sh_set_fpscr(observed);
    return NULL;
}

static bool verify_thread_fpscr(void) {
    uint32_t saved = __builtin_sh_get_fpscr();
    const uint32_t modes = (1u << 18) | 3u;
    for(uint32_t rounding = 0; rounding <= 1; ++rounding) {
        fpscr_probe_t probe = {
            .expected = (saved & ~3u) | (1u << 18) | rounding
        };
        __builtin_sh_set_fpscr(probe.expected);
        kthread_t *thread = thd_create(false, fpscr_thread, &probe);
        __builtin_sh_set_fpscr(saved);
        if(!thread || thd_join(thread, NULL) < 0 || !probe.passed ||
           (__builtin_sh_get_fpscr() & modes) != (saved & modes))
            return false;
    }
    return true;
}

static int compact_begin_strip(const pvr_chunk_render_state_t *state,
                               const pvr_chunk_strip_view_t *strip,
                               void *data) {
    size_t *calls = data;

    if(!(state->present & PVR_CHUNK_RENDER_DIFFUSE) ||
       state->diffuse_argb != UINT32_C(0xff208040) ||
       strip->vertex_count != 3) {
        errno = EILSEQ;
        return -1;
    }

    ++*calls;
    return 0;
}

static bool close_enough(float actual, float expected) {
    float scale = fmaxf(1.0f, fmaxf(fabsf(actual), fabsf(expected)));

    return isfinite(actual) && isfinite(expected) &&
           fabsf(actual - expected) <= 0.0001f * scale;
}

static int invalid_cached_position(const pvr_chunk_render_state_t *state,
                                   uint16_t index,
                                   const pvr_deform_vertex_t *deformation,
                                   pvr_vertex_t *vertex, void *data) {
    (void)state;
    (void)index;
    (void)deformation;
    (void)data;
    vertex->x = NAN;
    return 0;
}

static int prepare_two_volume_probe(const pvr_chunk_render_state_t *state,
    uint16_t index, const pvr_deform_vertex_t *deformation,
    pvr_geometry_vertex_format_t format, pvr_chunk_two_volume_vertex_t *vertex,
    void *data) {
    (void)state; (void)index; (void)deformation;
    if(data) {
        vertex->color.x = NAN;
        return 0;
    }
    if(format == PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR) {
        unsigned char *bytes = (unsigned char *)vertex;
        for(size_t i = 32; i < sizeof(*vertex); ++i)
            if(bytes[i]) return -1;
        memset(bytes + 32, 0xcc, sizeof(*vertex) - 32);
        vertex->color.argb1 ^= 0x1234;
    }
    else {
        vertex->textured.u1 += 0.125f;
        vertex->textured.argb1 ^= 0x1234;
    }
    return 0;
}

static bool verify_two_volume_draws(const pvr_chunk_model_t *base) {
    static const uint16_t color[] = {
        PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0x2233, 0xff11,
        PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 2, 0x5566, 0xff44,
        PVR_CHUNK_STRIP_TWO_VOLUME, 5, 1, 3, 0, 1, 2, 0xff
    };
    static const uint16_t textured[] = {
        PVR_CHUNK_MATERIAL_DIFFUSE, 2, 0x2233, 0xff11,
        PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME, 2, 0x5566, 0xff44,
        PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME, 17, 1, 3,
        0, 0, 0, 256, 256,
        1, 128, 0, 64, 128,
        2, 256, 128, 0, 64, 0xff
    };
    pvr_chunk_vertex_index_entry_t indices[256];
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_two_volume_cache_t cache;
    pvr_chunk_two_volume_cache_draw_t draw;
    pvr_chunk_cache_result_t result;
    pvr_geometry_vertex_sink_t sink;
    uint8_t storage[2048] __attribute__((aligned(32)));
    uint8_t expected[224] __attribute__((aligned(32)));
    uint8_t actual[224] __attribute__((aligned(32)));
    pvr_chunk_two_volume_vertex_t workspace[3] __attribute__((aligned(32)));
    matrix_t matrix = {
        { 2, 0, 0, 0 }, { 0, 3, 0, 0 },
        { 0, 0, 1, 0 }, { 4, 5, 0, 1 }
    };
    shz_mat4x4_t saved, observed;

    shz_xmtrx_store_4x4(&saved);
    for(unsigned kind = 0; kind < 2; ++kind) {
        pvr_chunk_model_t model = *base;
        model.polygon_words = kind ? textured : color;
        model.polygon_word_count = kind ? sizeof(textured) / sizeof(*textured) :
                                          sizeof(color) / sizeof(*color);
        if(pvr_chunk_model_open(&model, &view) < 0 ||
           pvr_chunk_model_plan_build(&view, indices, 256, &plan) < 0 ||
           pvr_chunk_model_two_volume_cache_build(&plan, storage, sizeof(storage),
                                                  NULL, NULL, &cache) < 0 ||
           pvr_chunk_model_two_volume_cache_draw_prepare(&cache, &draw) < 0 ||
           cache.vertex_size != (kind ? 64u : 32u))
            return false;
        for(unsigned policy = 0; policy < 2; ++policy) {
            pvr_chunk_cache_prepare_two_volume_vertex_t prepare =
                policy ? prepare_two_volume_probe : NULL;
            memset(expected, 0x5a, sizeof(expected));
            memset(actual, 0x5a, sizeof(actual));
            if(pvr_geometry_vertex_sink_init_memory(&sink, cache.format, expected, 3) < 0 ||
               pvr_chunk_model_two_volume_cache_emit(&cache, &matrix, &sink,
                   workspace, 3, NULL, NULL, prepare, NULL, &result) < 0 ||
               pvr_geometry_vertex_sink_init_memory(&sink, cache.format, actual, 3) < 0 ||
               pvr_chunk_model_two_volume_cache_draw_emit(&draw, &matrix, &sink,
                   workspace, 3, NULL, NULL, NULL, prepare, NULL, &result) < 0 ||
               result.emitted_vertices != 3 || result.emitted_strips != 1 ||
               memcmp(expected, actual, sizeof(expected)))
                return false;
            shz_xmtrx_store_4x4(&observed);
            if(memcmp(&saved, &observed, sizeof(saved))) return false;
        }
        for(unsigned rejection = 0; rejection < 2; ++rejection) {
            matrix[3][3] = rejection ? 0 : 1;
            if(pvr_geometry_vertex_sink_init_memory(&sink, cache.format, actual, 3) < 0)
                return false;
            errno = 0;
            if(pvr_chunk_model_two_volume_cache_draw_emit(&draw, &matrix, &sink,
                   workspace, 3, NULL, NULL, NULL,
                   rejection ? NULL : prepare_two_volume_probe, &kind, &result) != -1 ||
               errno != EDOM || sink.emitted_vertices || result.emitted_vertices ||
               memcmp(expected, actual, sizeof(expected)))
                return false;
            shz_xmtrx_store_4x4(&observed);
            if(memcmp(&saved, &observed, sizeof(saved))) return false;
        }
        matrix[3][3] = 1;
    }
    return true;
}

typedef struct modifier_probe {
    unsigned calls;
    int rejection;
} modifier_probe_t;

static int prepare_modifier_probe(
    const uint16_t indices[3], const pvr_deform_vertex_t deformations[3],
    const uint16_t *words, size_t word_count,
    pvr_modifier_vol_t *triangle, void *data) {
    modifier_probe_t *probe = data;
    if(!indices || !deformations || word_count != 1 ||
       words[0] != (probe->calls & 1 ? 0xbb : 0xaa)) {
        errno = EILSEQ;
        return -1;
    }
    triangle->d1 = 0x12340000 | indices[2];
    if(probe->calls++ == 1) {
        switch(probe->rejection) {
            case 0: triangle->ax = NAN; break;
            case 1: triangle->by = NAN; break;
            case 2: triangle->cz = NAN; break;
            case 3: triangle->az = -1; break;
            case 4: triangle->bz = -1; break;
            case 5: triangle->cz = -1; break;
        }
    }
    return 0;
}

static bool verify_modifier_draws(const pvr_chunk_model_t *base) {
    static const uint16_t polygons[] = {
        PVR_CHUNK_VOLUME_TRIANGLES, 9, 0x4002,
        0, 1, 2, 0xaa, 2, 1, 0, 0xbb, 0xff
    };
    pvr_chunk_model_t model = *base;
    pvr_chunk_vertex_index_entry_t indices[256];
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_modifier_cache_t cache;
    pvr_chunk_modifier_cache_draw_t draw;
    pvr_chunk_modifier_cache_result_t result;
    pvr_chunk_modifier_config_t config = {
        PVR_LIST_OP_MOD, PVR_CULLING_NONE, PVR_MODIFIER_INCLUDE_LAST_POLY
    };
    pvr_geometry_vertex_sink_t sink;
    uint8_t storage[2048] __attribute__((aligned(32)));
    pvr_modifier_vol_t output[2][3] __attribute__((aligned(32)));
    pvr_modifier_vol_t workspace[2] __attribute__((aligned(32)));
    matrix_t matrix = {
        { 2, 0, 0, 0 }, { 0, 3, 0, 0 },
        { 0, 0, 1, 1 }, { 4, 5, 0, 1 }
    };
    shz_mat4x4_t saved, observed;

    shz_xmtrx_store_4x4(&saved);
    model.polygon_words = polygons;
    model.polygon_word_count = sizeof(polygons) / sizeof(*polygons);
    if(pvr_chunk_model_open(&model, &view) < 0 ||
       pvr_chunk_model_plan_build(&view, indices, 256, &plan) < 0 ||
       pvr_chunk_model_modifier_cache_build(&plan, storage, sizeof(storage),
                                             NULL, NULL, &cache) < 0 ||
       pvr_chunk_model_modifier_cache_draw_prepare(&cache, &draw) < 0)
        return false;
    /* -2: no callback; -1: successful callback; 0..5: bad A/B/C or W. */
    for(int rejection = -2; rejection < 6; ++rejection) {
        memset(output, 0x5a, sizeof(output));
        for(unsigned admitted = 0; admitted < 2; ++admitted) {
            modifier_probe_t probe = { 0, rejection };
            pvr_chunk_cache_prepare_modifier_t prepare =
                rejection == -2 ? NULL : prepare_modifier_probe;
            if(pvr_geometry_vertex_sink_init_memory(&sink,
                PVR_GEOMETRY_VERTEX_MODIFIER, output[admitted], 2) < 0)
                return false;
            errno = 0;
            int rv = admitted ? pvr_chunk_model_modifier_cache_draw_emit(
                &draw, &matrix, &config, &sink, workspace + admitted,
                NULL, prepare, &probe, &result) :
                pvr_chunk_model_modifier_cache_emit(&cache, &matrix, &config,
                &sink, workspace + admitted, NULL, prepare, &probe, &result);
            if(rv != (rejection < 0 ? 0 : -1) ||
               (rejection >= 0 && errno != EDOM) ||
               sink.emitted_vertices != (rejection < 0 ? 2u : 1u) ||
               result.emitted_triangles != sink.emitted_vertices ||
               result.emitted_volumes != (rejection < 0 ? 1u : 0u))
                return false;
            shz_xmtrx_store_4x4(&observed);
            if(memcmp(&saved, &observed, sizeof(saved))) return false;
            for(size_t i = 0; i < sizeof(output[admitted][2]); ++i)
                if(((uint8_t *)&output[admitted][2])[i] != 0x5a) return false;
        }
        if(memcmp(output[0], output[1], sizeof(output[0])) ||
           memcmp(workspace, workspace + 1, sizeof(*workspace)))
            return false;
        if(!close_enough(output[1][0].ax, 2.0f) ||
           !close_enough(output[1][0].by, 2.0f) ||
           !close_enough(output[1][0].cx, 4.0f) ||
           !close_enough(output[1][0].cy, 8.0f))
            return false;
    }
    return true;
}

static void show_result(bool passed, const char *detail) {
    printf("RESULT: %s (%s)\n", passed ? "PASS" : "FAIL", detail);
    fflush(stdout);

    vid_clear(passed ? 0 : 96, passed ? 96 : 0, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT,
                   vid_mode->width, true,
                   passed ? "SH4ZAM/KOS integration: PASS"
                          : "SH4ZAM/KOS integration: FAIL");
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT * 3,
                   vid_mode->width, true, detail);
    thd_sleep(15000);
}

#define FAIL(detail) do {       \
    show_result(false, detail); \
    return 1;                   \
} while(0)

static void math_fiber(void *data) {
    const shz_mat4x4_t *identity = data;
    shz_mat4x4_t observed;

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, identity, sizeof(observed))) {
        fiber_result = 1;
        return;
    }

    shz_xmtrx_init_translation(7.0f, 8.0f, 9.0f);
    shz_xmtrx_store_4x4(&fiber_matrix);
    if(fiber_switch(main_fiber) < 0) {
        fiber_result = 2;
        return;
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &fiber_matrix, sizeof(observed)))
        fiber_result = 3;
}

int main(int argc, char **argv) {
    shz_mat4x4_t source;
    shz_mat4x4_t imported;
    shz_mat4x4_t identity;
    shz_mat4x4_t observed;
    matrix_t established;
    vector_t established_vector = { 1.0f, 2.0f, 3.0f, 1.0f };
    vector_t round_trip_vector;
    shz_vec4_t vector;
    pvr_vertex_t invalid_geometry[2] __attribute__((aligned(32)));
    pvr_vertex_t projected[2] __attribute__((aligned(32)));
    pvr_geometry_stream_t geometry_stream = {
        geometry_input, 2, sizeof(*geometry_input)
    };
    pvr_geometry_result_t geometry_result;
    pvr_chunk_model_t compact_model = {
        .vertex_words = compact_vertices,
        .vertex_word_count = sizeof(compact_vertices) /
                             sizeof(compact_vertices[0]),
        .polygon_words = compact_polygons,
        .polygon_word_count = sizeof(compact_polygons) /
                              sizeof(compact_polygons[0]),
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 2.0f
    };
    pvr_chunk_model_view_t compact_view;
    pvr_geometry_sink_t compact_sink;
    pvr_chunk_render_result_t compact_result;
    pvr_vertex_t compact_workspace[3] __attribute__((aligned(32)));
    pvr_vertex_t compact_output[3] __attribute__((aligned(32)));
    size_t compact_strip_calls = 0;
    mat_lookat_desc_t lookat = {
        .eye = { 1.0f, 2.0f, 3.0f, 1.0f },
        .center = { 1.0f, 2.0f, 2.0f, 1.0f },
        .up = { 0.0f, 1.0f, 0.0f, 0.0f }
    };
    mat_perspective_desc_t perspective = {
        .x_center = 320.0f,
        .y_center = 240.0f,
        .cot_half_fov = 1.0f,
        .z_near = 1.0f,
        .z_far = 100.0f
    };
    matrix_t camera;
    pvr_frustum_t frustum;
    point_t bounds_minimum = { -0.5f, -0.5f, -0.5f, 1.0f };
    point_t bounds_maximum = { 0.5f, 0.5f, 0.5f, 1.0f };
    pvr_frustum_classification_t classification;
    pvr_vertex_t clip_input[3] __attribute__((aligned(32))) = {
        { .flags = PVR_CMD_VERTEX, .x = -2.0f, .y = -0.5f,
          .argb = UINT32_C(0xff000000) },
        { .flags = PVR_CMD_VERTEX, .x = 0.5f, .y = -0.5f,
          .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 0.0f, .y = 0.5f,
          .argb = UINT32_C(0xff808080) }
    };
    pvr_vertex_t clipped[PVR_FRUSTUM_CLIP_MAX_VERTICES]
        __attribute__((aligned(32)));
    pvr_frustum_clip_result_t clip_result;
    uint8_t major;
    uint16_t minor;
    uint8_t patch;
    kfiber_t *fiber;

    (void)argc;
    (void)argv;

    (void)dbgio_dev_select("scif");

    if(!verify_thread_fpscr()) {
        FAIL("thread FPSCR inheritance/isolation");
    }
    puts("SH4ZAM thread FPSCR inheritance/isolation: PASS");

    /* Distinct values in every XMTRX lane detect partial clobbers too. */
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 4; ++r)
            matrix_sentinel.elem2D[c][r] = (float)(1 + c * 4 + r);
    shz_xmtrx_load_4x4(&matrix_sentinel);
    const char *matrix_failure = verify_animation_matrices(
        matrix_state_unchanged, 0.0003);
    if(matrix_failure)
        FAIL(matrix_failure);
    puts("SH4ZAM TRS, rolled camera, compose aliasing, XMTRX: PASS");

    shz_mat4x4_init_translation(&source, 4.0f, 5.0f, 6.0f);
    shz_kos_matrix_export(&established, &source);
    shz_kos_matrix_import(&imported, &established);

    if(memcmp(&source, &imported, sizeof(source))) {
        FAIL("matrix bridge");
    }

    shz_mat4x4_init_identity(&identity);
    shz_xmtrx_load_4x4(&source);
    main_fiber = fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!main_fiber ||
       fiber_get_attach_flags() != KFIBER_ATTACH_MATH_CONTEXT) {
        FAIL("fiber math attachment");
    }

    fiber = fiber_create(fiber_stack, sizeof(fiber_stack), math_fiber,
                         &identity);
    if(!fiber || fiber_switch(fiber) < 0) {
        FAIL("first fiber transfer");
    }

    shz_xmtrx_store_4x4(&observed);
    if(fiber_result || memcmp(&observed, &source, sizeof(observed)) ||
       fiber_switch(fiber) < 0 || fiber_result ||
       fiber_get_state(fiber) != KFIBER_STATE_FINISHED) {
        FAIL("XMTRX fiber isolation");
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &source, sizeof(observed)) ||
       fiber_destroy(fiber) < 0) {
        FAIL("main XMTRX restoration");
    }

    /* The target geometry path keeps a transform resident in XMTRX across the
       batch, then restores the caller's matrix before publishing completion. */
    shz_kos_matrix_export(&established, &identity);
    if(pvr_geometry_project(projected, 2, &geometry_stream, &established,
                            &geometry_result) < 0 ||
       geometry_result.consumed_vertices != 2 ||
       geometry_result.produced_vertices != 2 ||
       projected[0].x != 2.0f || projected[0].y != 3.0f ||
       projected[0].z != 1.0f ||
       projected[1].x != -2.0f || projected[1].y != -3.0f ||
       projected[1].z != 1.0f ||
       projected[0].argb != geometry_input[0].argb) {
        FAIL("SH4ZAM geometry projection");
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &source, sizeof(observed)))
        FAIL("geometry XMTRX restoration");

    memcpy(invalid_geometry, geometry_input, sizeof(invalid_geometry));
    invalid_geometry[1].flags = 0;
    geometry_stream.vertices = invalid_geometry;
    errno = 0;
    if(pvr_geometry_project(projected, 2, &geometry_stream, &established,
                            &geometry_result) != -1 || errno != EILSEQ ||
       geometry_result.consumed_vertices != 1 ||
       geometry_result.produced_vertices != 1) {
        FAIL("partial geometry rejection");
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &source, sizeof(observed)))
        FAIL("rejected geometry XMTRX restoration");

    /* Compact models remain caller-owned. The bounded bridge resolves their
       strips into canonical geometry, uses the same SH4ZAM projection path,
       and publishes only through an explicit sink. */
    geometry_stream.vertices = geometry_input;
    if(pvr_chunk_model_open(&compact_model, &compact_view) < 0 ||
       pvr_geometry_sink_init_memory(&compact_sink, compact_output, 3) < 0 ||
       pvr_chunk_model_emit(&compact_view, &established, &compact_sink,
                            compact_workspace, 3, compact_begin_strip, NULL,
                            &compact_strip_calls, &compact_result) < 0 ||
       compact_strip_calls != 1 || compact_result.emitted_strips != 1 ||
       compact_result.emitted_vertices != 3 ||
       compact_output[0].x != -1.0f || compact_output[0].y != -1.0f ||
       compact_output[1].x != 1.0f || compact_output[1].y != -1.0f ||
       compact_output[2].x != 0.0f || compact_output[2].y != 1.0f ||
       compact_output[0].argb != UINT32_C(0xff208040) ||
       compact_output[2].flags != PVR_CMD_VERTEX_EOL) {
        FAIL("compact model emission");
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &source, sizeof(observed)))
        FAIL("compact model XMTRX restoration");

    {
        pvr_chunk_vertex_index_entry_t indices[256];
        pvr_chunk_model_plan_t plan;
        pvr_chunk_model_cache_t cache;
        pvr_chunk_cache_draw_t draw;
        pvr_chunk_cache_result_t result;
        uint8_t storage[2048] __attribute__((aligned(32)));
        pvr_vertex_t expected[3];

        memcpy(expected, compact_output, sizeof(expected));
        if(pvr_chunk_model_plan_build(&compact_view, indices, 256, &plan) < 0 ||
           pvr_chunk_model_cache_build(&plan, storage, sizeof(storage),
                                       NULL, NULL, &cache) < 0 ||
           pvr_chunk_model_cache_draw_prepare(&cache, &draw) < 0 ||
           pvr_geometry_sink_init_memory(&compact_sink, compact_output, 3) < 0 ||
           pvr_chunk_model_cache_draw_emit(&draw, &established, &compact_sink,
               compact_workspace, 3, NULL, NULL, NULL, NULL, NULL, &result) < 0 ||
           result.emitted_vertices != 3 || result.emitted_strips != 1 ||
           memcmp(expected, compact_output, sizeof(expected)))
            FAIL("admitted compact draw equivalence");
        shz_xmtrx_store_4x4(&observed);
        if(memcmp(&observed, &source, sizeof(observed)))
            FAIL("admitted compact draw XMTRX");
        for(unsigned rejection = 0; rejection < 2; ++rejection) {
            if(rejection)
                established[3][3] = 0.0f;
            if(pvr_geometry_sink_init_memory(&compact_sink, compact_output, 3) < 0)
                FAIL("admitted compact sink");
            errno = 0;
            if(pvr_chunk_model_cache_draw_emit(&draw, &established, &compact_sink,
                   compact_workspace, 3, NULL, NULL, NULL,
                   rejection ? NULL : invalid_cached_position, NULL, &result) != -1 ||
               errno != EDOM || compact_sink.emitted_vertices ||
               memcmp(expected, compact_output, sizeof(expected)))
                FAIL("admitted compact rejection");
            shz_xmtrx_store_4x4(&observed);
            if(memcmp(&observed, &source, sizeof(observed)))
                FAIL("admitted rejected draw XMTRX");
        }
        shz_kos_matrix_export(&established, &identity);
        puts("Admitted Compact draw, rejection, XMTRX: PASS");
    }

    if(!verify_two_volume_draws(&compact_model))
        FAIL("admitted two-volume cache");
    puts("Admitted two-volume color/textured draws, rejection, XMTRX: PASS");

    if(!verify_modifier_draws(&compact_model))
        FAIL("admitted modifier cache");
    puts("Admitted modifier triangles, rejection, XMTRX: PASS");

    if(!toon_draw_fixtures())
        FAIL("admitted toon/outline cache");
    puts("Admitted toon/outline policies, clipping, rejection, XMTRX: PASS");

    /* Camera and frustum entry points retain their established checked
       contracts while their Dreamcast arithmetic runs through SH4ZAM. The
       one-off FIPR path must not disturb the fiber's resident XMTRX state. */
    if(mat_lookat_build(&camera, &lookat) < 0 ||
       !close_enough(camera[3][0], -1.0f) ||
       !close_enough(camera[3][1], -2.0f) ||
       !close_enough(camera[3][2], -3.0f) ||
       mat_perspective_build(&camera, &perspective) < 0 ||
       !close_enough(camera[2][2], -1.02020202f) ||
       !close_enough(camera[3][2], -2.02020202f)) {
        FAIL("SH4ZAM camera math");
    }

    shz_kos_matrix_export(&established, &identity);
    if(pvr_frustum_init(&frustum, &established, -1.0f, -1.0f,
                        1.0f, 1.0f, 0.5f, 2.0f) < 0 ||
       pvr_frustum_classify_aabb(&frustum, &bounds_minimum, &bounds_maximum,
                                 &classification) < 0 ||
       classification != PVR_FRUSTUM_INSIDE ||
       pvr_frustum_clip_triangle(clipped, PVR_FRUSTUM_CLIP_MAX_VERTICES,
                                 clip_input, &frustum,
                                 PVR_FRUSTUM_CLIP_ALL, &clip_result) < 0 ||
       clip_result.polygon_vertices != 4 ||
       clip_result.output_vertices != 6 ||
       clipped[0].x < -1.0001f || clipped[1].x < -1.0001f ||
       clipped[2].x < -1.0001f || clipped[3].x < -1.0001f ||
       clipped[4].x < -1.0001f || clipped[5].x < -1.0001f) {
        FAIL("SH4ZAM frustum math");
    }

    shz_xmtrx_store_4x4(&observed);
    if(memcmp(&observed, &source, sizeof(observed)))
        FAIL("camera/frustum XMTRX preservation");

    vector = shz_kos_vec4_import(&established_vector);
    if(vector.x != 1.0f || vector.y != 2.0f ||
       vector.z != 3.0f || vector.w != 1.0f) {
        FAIL("vector bridge");
    }

    round_trip_vector = shz_kos_vec4_export(vector);
    if(memcmp(&established_vector, &round_trip_vector,
              sizeof(established_vector))) {
        FAIL("vector round trip");
    }

    if(shz_version_linked() != SHZ_VERSION) {
        FAIL("header/library version mismatch");
    }

    shz_version_fields(shz_version_linked(), &major, &minor, &patch);
    if(major != 0 || minor != 8 || patch != 0) {
        FAIL("unexpected SH4ZAM version");
    }

    show_result(true, "SH4ZAM 0.8 camera, frustum, geometry, and fibers");
    return 0;
}
