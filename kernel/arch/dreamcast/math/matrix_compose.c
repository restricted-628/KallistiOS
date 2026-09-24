/* KallistiOS ##version##

   matrix_compose.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix.h>

#include <dc/sh4zam.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
}

int mat_compose(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs) {
    if(!out || !lhs || !rhs || !matrix_aligned(out) ||
       !matrix_aligned(lhs) || !matrix_aligned(rhs)) {
        errno = EINVAL;
        return -1;
    }

    shz_kos_matrix_compose_unchecked(out, lhs, rhs);
    return 0;
}
