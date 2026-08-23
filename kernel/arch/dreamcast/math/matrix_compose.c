/* KallistiOS ##version##

   matrix_compose.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/matrix.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

static int matrix_aligned(const matrix_t *matrix) {
    return !((uintptr_t)matrix & (_Alignof(matrix_t) - 1u));
}

int mat_compose(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs) {
    matrix_t result;
    size_t column;
    size_t row;

    if(!out || !lhs || !rhs || !matrix_aligned(out) ||
       !matrix_aligned(lhs) || !matrix_aligned(rhs)) {
        errno = EINVAL;
        return -1;
    }

    for(column = 0; column < 4; ++column) {
        for(row = 0; row < 4; ++row) {
            result[column][row] =
                (*lhs)[0][row] * (*rhs)[column][0] +
                (*lhs)[1][row] * (*rhs)[column][1] +
                (*lhs)[2][row] * (*rhs)[column][2] +
                (*lhs)[3][row] * (*rhs)[column][3];
        }
    }

    memcpy(out, &result, sizeof(result));
    return 0;
}
