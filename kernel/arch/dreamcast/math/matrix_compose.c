/* KallistiOS ##version##

   matrix_compose.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix.h>

#ifdef __DREAMCAST__
#include <dc/sh4zam.h>
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
}

int mat_compose(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs) {
    matrix_t result;
#ifdef __DREAMCAST__
    shz_mat4x4_t shz_lhs;
    shz_mat4x4_t shz_rhs;
    shz_mat4x4_t shz_result;
#else
    size_t column;
    size_t row;
#endif

    if(!out || !lhs || !rhs || !matrix_aligned(out) ||
       !matrix_aligned(lhs) || !matrix_aligned(rhs)) {
        errno = EINVAL;
        return -1;
    }

#ifdef __DREAMCAST__
    /* SH4ZAM's one-off vector path uses FIPR without loading XMTRX. Applying
       the left matrix to each right-hand column therefore preserves this
       function's no-global-state contract while accelerating the target. */
    shz_kos_matrix_import(&shz_lhs, lhs);
    shz_kos_matrix_import(&shz_rhs, rhs);
    shz_result.col[0] = shz_mat4x4_transform_vec4(&shz_lhs,
                                                   shz_rhs.col[0]);
    shz_result.col[1] = shz_mat4x4_transform_vec4(&shz_lhs,
                                                   shz_rhs.col[1]);
    shz_result.col[2] = shz_mat4x4_transform_vec4(&shz_lhs,
                                                   shz_rhs.col[2]);
    shz_result.col[3] = shz_mat4x4_transform_vec4(&shz_lhs,
                                                   shz_rhs.col[3]);
    shz_kos_matrix_export(&result, &shz_result);
#else
    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            result[column][row] =
                (*lhs)[0][row] * (*rhs)[column][0] +
                (*lhs)[1][row] * (*rhs)[column][1] +
                (*lhs)[2][row] * (*rhs)[column][2] +
                (*lhs)[3][row] * (*rhs)[column][3];
        }
    }
#endif

    memcpy(out, &result, sizeof(result));
    return 0;
}
