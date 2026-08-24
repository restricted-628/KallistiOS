/* KallistiOS ##version##

   SH4ZAM/KOS integration example
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/sh4zam.h>

#include <dc/biosfont.h>
#include <dc/video.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Use SCIF so emulator and hardware validation can capture a deterministic
   result without depending on a loader-provided debug console. */
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

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

static bool close_enough(float actual, float expected) {
    float scale = fmaxf(1.0f, fmaxf(fabsf(actual), fabsf(expected)));

    return isfinite(actual) && isfinite(expected) &&
           fabsf(actual - expected) <= 0.0001f * scale;
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
