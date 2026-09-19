/* KallistiOS ##version##

   dc/sh4zam.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/sh4zam.h
    \brief   Interoperation between established KOS math types and SH4ZAM.
    \ingroup math_matrices

    SH4ZAM is the primary optimized math implementation for new Dreamcast 3D
    facilities. This header provides explicit, alias-safe conversion helpers
    for established KOS types whose public representation cannot be changed.
*/

#ifndef __DC_SH4ZAM_H
#define __DC_SH4ZAM_H

#include <kos/cdefs.h>

#include <stddef.h>
#include <string.h>

#include <dc/vector.h>
#include <sh4zam/shz_sh4zam.h>

__BEGIN_DECLS

/** \defgroup math_sh4zam SH4ZAM Interoperation
    \brief First-class optimized math integration
    \ingroup math_matrices
    @{ */

#if defined(__cplusplus)
static_assert(sizeof(matrix_t) == sizeof(shz_mat4x4_t));
static_assert(alignof(matrix_t) == alignof(shz_mat4x4_t));
static_assert(sizeof(vector_t) == sizeof(shz_vec4_t));
static_assert(offsetof(vector_t, x) == offsetof(shz_vec4_t, x));
static_assert(offsetof(vector_t, y) == offsetof(shz_vec4_t, y));
static_assert(offsetof(vector_t, z) == offsetof(shz_vec4_t, z));
static_assert(offsetof(vector_t, w) == offsetof(shz_vec4_t, w));
#else
_Static_assert(sizeof(matrix_t) == sizeof(shz_mat4x4_t));
_Static_assert(_Alignof(matrix_t) == _Alignof(shz_mat4x4_t));
_Static_assert(sizeof(vector_t) == sizeof(shz_vec4_t));
_Static_assert(offsetof(vector_t, x) == offsetof(shz_vec4_t, x));
_Static_assert(offsetof(vector_t, y) == offsetof(shz_vec4_t, y));
_Static_assert(offsetof(vector_t, z) == offsetof(shz_vec4_t, z));
_Static_assert(offsetof(vector_t, w) == offsetof(shz_vec4_t, w));
#endif

/** \brief Import an established KOS matrix into a SH4ZAM matrix.

    The representations have identical size, alignment, component order, and
    column-major layout. Copying through this helper avoids violating the C and
    C++ aliasing rules. New performance-sensitive code should retain
    `shz_mat4x4_t` throughout its pipeline instead of repeatedly converting.
*/
static inline void shz_kos_matrix_import(shz_mat4x4_t *dst,
                                         const matrix_t *src) {
    memcpy(dst, src, sizeof(*dst));
}

/** \brief Export a SH4ZAM matrix into an established KOS matrix. */
static inline void shz_kos_matrix_export(matrix_t *dst,
                                         const shz_mat4x4_t *src) {
    memcpy(dst, src, sizeof(*src));
}

/** \brief Import an established KOS four-component vector. */
static inline shz_vec4_t shz_kos_vec4_import(const vector_t *src) {
    shz_vec4_t dst;

    memcpy(&dst, src, sizeof(dst));
    return dst;
}

/** \brief Export a SH4ZAM four-component vector. */
static inline vector_t shz_kos_vec4_export(shz_vec4_t src) {
    vector_t dst;

    memcpy(&dst, &src, sizeof(dst));
    return dst;
}

/** @} */

__END_DECLS
#endif /* __DC_SH4ZAM_H */
