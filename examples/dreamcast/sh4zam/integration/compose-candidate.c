/* KallistiOS ##version##
   Experimental XMTRX-preserving SH4ZAM multiply, for comparison only.
   Copyright (C) 2026 Joseph Black
*/
#include "compose-candidate.h"
#include <dc/sh4zam.h>
#include <errno.h>
#include <stdint.h>

int compose_candidate(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs) {
    shz_mat4x4_t saved, left, right, result;
    if(!out || !lhs || !rhs ||
       ((uintptr_t)out & (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)lhs & (_Alignof(matrix_t) - 1u)) ||
       ((uintptr_t)rhs & (_Alignof(matrix_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    /* Preserve aliasing and type rules: no casts between matrix object types.
       Unlike mat_compose's FIPR path, mult temporarily changes XMTRX. */
    shz_kos_matrix_import(&left, lhs);
    shz_kos_matrix_import(&right, rhs);
    shz_xmtrx_store_4x4(&saved);
    shz_mat4x4_mult(&result, &left, &right);
    shz_xmtrx_load_4x4(&saved);
    shz_kos_matrix_export(out, &result);
    return 0;
}
