/* KallistiOS ##version##

   matrix3d.h
   (c)2000 Megan Potter and Jordan DeLong
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/matrix3d.h
    \brief   3D matrix operations.
    \ingroup math_matrices

    This file contains various 3D matrix math functionality for using the SH4's
    matrix transformation unit.

    \author Megan Potter
    \author Jordan DeLong
*/

#ifndef __KOS_MATRIX3D_H
#define __KOS_MATRIX3D_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/matrix.h>

/** \addtogroup math_matrices
    @{
*/

/** \brief Explicit parameters for the established perspective transform.

    This descriptor represents the same screen-space and finite-frustum
    convention as mat_perspective(), but can be validated and built into a
    caller-owned matrix without changing XMTRX.
*/
typedef struct mat_perspective_desc {
    float x_center;    /**< Horizontal screen-space center. */
    float y_center;    /**< Vertical center and screen-space scale. */
    float cot_half_fov;/**< Cotangent of half the vertical field of view. */
    float z_near;      /**< Positive near clipping distance. */
    float z_far;       /**< Far clipping distance, greater than z_near. */
} mat_perspective_desc_t;

/** \brief Explicit look-at view description.

    Only the XYZ components are used. The eye and center must differ, the up
    vector must be nonzero, and up must not be parallel to the viewing
    direction.
*/
typedef struct mat_lookat_desc {
    point_t eye;       /**< Camera position. */
    point_t center;    /**< Point observed by the camera. */
    vector_t up;       /**< Approximate upward direction. */
} mat_lookat_desc_t;

/** \brief Build a checked perspective matrix in caller-owned storage.

    The output is not modified on failure. The destination must satisfy
    matrix_t's alignment requirement. This function does not change XMTRX.

    \param out         Destination matrix.
    \param desc        Perspective description.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL`, `EDOM`, or `ERANGE`.
*/
int mat_perspective_build(matrix_t *out,
                          const mat_perspective_desc_t *desc);

/** \brief Validate and apply an explicit perspective transform to XMTRX.

    XMTRX remains unchanged when validation fails.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL`, `EDOM`, or `ERANGE`.
*/
int mat_perspective_apply(const mat_perspective_desc_t *desc);

/** \brief Build a checked look-at view matrix in caller-owned storage.

    The output is not modified on failure. The destination must satisfy
    matrix_t's alignment requirement. This function does not change XMTRX.

    \param out         Destination matrix.
    \param desc        Look-at view description.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL`, `EDOM`, or `ERANGE`.
*/
int mat_lookat_build(matrix_t *out, const mat_lookat_desc_t *desc);

/** \brief Validate and apply an explicit look-at view transform to XMTRX.

    XMTRX remains unchanged when validation fails.

    \retval 0  Success.
    \retval -1 Error, with `errno` set to `EINVAL`, `EDOM`, or `ERANGE`.
*/
int mat_lookat_apply(const mat_lookat_desc_t *desc);

/** \brief  Rotate around the X-axis.

    This function sets up a rotation matrix around the X-axis.

    \param  r               The angle to rotate, in radians.
*/
void mat_rotate_x(float r);

/** \brief  Rotate around the Y-axis.

    This function sets up a rotation matrix around the Y-axis.

    \param  r               The angle to rotate, in radians.
*/
void mat_rotate_y(float r);

/** \brief  Rotate around the Z-axis.

    This function sets up a rotation matrix around the Z-axis.

    \param  r               The angle to rotate, in radians.
*/
void mat_rotate_z(float r);

/** \brief  Rotate around all axes.

    This function sets up a rotation matrix around the X-axis, then around the
    Y, then around the Z.

    \param  xr              The angle to rotate around the X-axis, in radians.
    \param  yr              The angle to rotate around the Y-axis, in radians.
    \param  zr              The angle to rotate around the Z-axis, in radians.
*/
void mat_rotate(float xr, float yr, float zr);

/** \brief  Perform a 3D translation.

    This function sets up a translation matrix with the specified parameters.

    \param  x               The amount to translate in X.
    \param  y               The amount to translate in Y.
    \param  z               The amount to translate in Z.
*/
void mat_translate(float x, float y, float z);

/** \brief  Perform a 3D scale operation.

    This function sets up a scaling matrix with the specified parameters.

    \param  x               The ratio to scale in X.
    \param  y               The ratio to scale in Y.
    \param  z               The ratio to scale in Z.
*/
void mat_scale(float x, float y, float z);

/** \brief  Set up a perspective view frustum.

    This function sets up a perspective view frustum for basic 3D usage.

    \param  xcenter         Center of the X direction.
    \param  ycenter         Center of the Y direction.
    \param  cot_fovy_2      1.0 / tan(view_angle / 2).
    \param  znear           Near Z-plane.
    \param  zfar            Far Z-plane.
*/
void mat_perspective(float xcenter, float ycenter, float cot_fovy_2,
                     float znear, float zfar);

/** \brief  Set up a "camera".

    This function acts as the similarly named GL function to set up a "camera"
    by doing rotations/translations.

    \param  eye             The eye coordinate.
    \param  center          The center coordinate.
    \param  up              The up vector.
*/
void mat_lookat(const point_t * eye, const point_t * center, const vector_t * up);

/** @} */

__END_DECLS

#endif  /* __KOS_MATRIX3D_H */

