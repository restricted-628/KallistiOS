/* KallistiOS ##version##

   Checked camera matrix example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static int matrices_close(const matrix_t *lhs, const matrix_t *rhs) {
    size_t column;
    size_t row;

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            float difference = (*lhs)[column][row] - (*rhs)[column][row];

            if(!isfinite(difference) || fabsf(difference) >= 0.0001f)
                return 0;
        }
    }

    return 1;
}

int main(int argc, char **argv) {
    const mat_perspective_desc_t perspective = {
        320.0f, 240.0f, 1.0f, 1.0f, 100.0f
    };
    const mat_lookat_desc_t view = {
        { 1.0f, 2.0f, 3.0f, 1.0f },
        { 1.0f, 2.0f, 2.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f }
    };
    mat_perspective_desc_t invalid = perspective;
    alignas(32) matrix_t legacy;
    alignas(32) matrix_t checked;
    alignas(32) matrix_t projection;
    alignas(32) matrix_t camera;
    alignas(32) matrix_t composed;
    alignas(32) matrix_t current;

    (void)argc;
    (void)argv;

    /* The checked builders preserve the established KOS transform order. */
    mat_identity();
    mat_perspective(perspective.x_center, perspective.y_center,
                    perspective.cot_half_fov, perspective.z_near,
                    perspective.z_far);
    mat_store(&legacy);
    assert(mat_perspective_build(&checked, &perspective) == 0);
    assert(matrices_close(&legacy, &checked));

    mat_identity();
    mat_lookat(&view.eye, &view.center, &view.up);
    mat_store(&legacy);
    assert(mat_lookat_build(&checked, &view) == 0);
    assert(matrices_close(&legacy, &checked));

    /* Memory composition has the same post-multiply order as mat_apply(). */
    assert(mat_perspective_build(&projection, &perspective) == 0);
    assert(mat_lookat_build(&camera, &view) == 0);
    assert(mat_compose(&composed, &projection, &camera) == 0);
    mat_load(&projection);
    mat_apply(&camera);
    mat_store(&current);
    assert(matrices_close(&composed, &current));

    /* A rejected descriptor does not disturb either output or XMTRX. */
    invalid.z_far = invalid.z_near;
    memcpy(&checked, &current, sizeof(checked));
    errno = 0;
    assert(mat_perspective_build(&checked, &invalid) == -1);
    assert(errno == EDOM);
    assert(memcmp(&checked, &current, sizeof(checked)) == 0);

    errno = 0;
    assert(mat_perspective_apply(&invalid) == -1);
    assert(errno == EDOM);
    mat_store(&checked);
    assert(memcmp(&checked, &current, sizeof(checked)) == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2,
                   vid_mode->width, 1,
                   "RESULT: PASS (checked camera matrices)");
    puts("RESULT: PASS (checked camera matrices)");

    for(;;)
        thd_sleep(1000);
}
