/* KallistiOS ##version##

   SH4ZAM/KOS integration example
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <dc/sh4zam.h>

#include <dc/biosfont.h>
#include <dc/video.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Use SCIF so emulator and hardware validation can capture a deterministic
   result without depending on a loader-provided debug console. */
KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NO_DCLOAD);

static uint8_t fiber_stack[2048] __attribute__((aligned(THD_STACK_ALIGNMENT)));
static kfiber_t *main_fiber;
static shz_mat4x4_t fiber_matrix;
static int fiber_result;

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

    show_result(true, "SH4ZAM 0.8 and opt-in XMTRX fibers");
    return 0;
}
