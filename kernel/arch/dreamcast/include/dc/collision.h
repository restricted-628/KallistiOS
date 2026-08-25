/* KallistiOS ##version##

   dc/collision.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/collision.h
    \brief   Checked, allocation-free collision geometry primitives.
    \ingroup collision

    This interface owns no world, broad phase, object, callback, or storage.
    All input geometry is caller-owned and all operations are synchronous.
*/

#ifndef __DC_COLLISION_H
#define __DC_COLLISION_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>

#include <dc/vector.h>

/** \defgroup collision Collision geometry
    \brief                  Renderer-independent bounded geometry queries
    \ingroup                math_matrices
    @{ */

/** \brief Finite line segment including both endpoints. */
typedef struct collision_segment {
    point_t start;
    point_t end;
} collision_segment_t;

/** \brief Half-line beginning at an origin and extending along a direction.

    The direction must be finite and nonzero, but need not be normalized.
    Ray-query distances are measured in world units rather than multiples of
    the supplied direction.
*/
typedef struct collision_ray {
    point_t origin;
    vector_t direction;
} collision_ray_t;

/** \brief Triangle including its boundary and preserving winding order. */
typedef struct collision_triangle {
    point_t first;
    point_t second;
    point_t third;
} collision_triangle_t;

/** \brief Plane represented by `dot(normal, point) + offset == 0`.

    Planes published by collision_plane_from_points() have a unit normal.
*/
typedef struct collision_plane {
    vector_t normal;
    float offset;
} collision_plane_t;

/** \brief Sphere with a finite, nonnegative radius. */
typedef struct collision_sphere {
    point_t center;
    float radius;
} collision_sphere_t;

/** \brief Capsule formed by a segment swept by a sphere.

    Equal endpoints are valid and reduce the capsule to a sphere.
*/
typedef struct collision_capsule {
    point_t start;
    point_t end;
    float radius;
} collision_capsule_t;

/** \brief Axis-aligned bounding box with inclusive extrema. */
typedef struct collision_aabb {
    point_t minimum;
    point_t maximum;
} collision_aabb_t;

/** \brief Oriented box described by a center, basis, and half extents.

    The three axes must be finite, unit length, and mutually orthogonal. The
    corresponding XYZ half extents must be finite and nonnegative. All W
    components are ignored.
*/
typedef struct collision_obb {
    point_t center;
    vector_t axes[3];
    vector_t half_extents;
} collision_obb_t;

/** \brief Closest point selected on a segment. */
typedef struct collision_closest_point {
    point_t point;
    float parameter;
    float squared_distance;
} collision_closest_point_t;

/** \brief Closest pair selected on two segments. */
typedef struct collision_closest_pair {
    point_t first_point;
    point_t second_point;
    float first_parameter;
    float second_parameter;
    float squared_distance;
} collision_closest_pair_t;

/** \brief Signed distance and projection of a point against a plane. */
typedef struct collision_plane_projection {
    point_t point;
    float signed_distance;
} collision_plane_projection_t;

/** \brief Closest point and barycentric coordinates on a triangle. */
typedef struct collision_triangle_closest {
    point_t point;
    float first_weight;
    float second_weight;
    float third_weight;
    float squared_distance;
} collision_triangle_closest_t;

/** \brief Detailed ray hit against a triangle. */
typedef struct collision_triangle_hit {
    point_t point;
    vector_t normal;
    float distance;
    float first_weight;
    float second_weight;
    float third_weight;
} collision_triangle_hit_t;

/** \brief Entry and exit interval selected by a ray-volume query. */
typedef struct collision_ray_interval {
    point_t entry_point;
    point_t exit_point;
    float entry_distance;
    float exit_distance;
} collision_ray_interval_t;

/** \brief Build a unit plane from three counter-clockwise points.

    XYZ components must be finite and the triangle must have nonzero area.
    Input W components are ignored. The output normal has W zero. Failure
    leaves \p output unchanged.
*/
int collision_plane_from_points(const point_t *first, const point_t *second,
                                const point_t *third,
                                collision_plane_t *output);

/** \brief Find the closest point on a segment.

    The published parameter is in `[0, 1]`, the point has W one, and the
    squared distance is measured from \p point. Equal segment endpoints are
    valid. Failure leaves \p output unchanged.
*/
int collision_point_segment_closest(const point_t *point,
                                    const collision_segment_t *segment,
                                    collision_closest_point_t *output);

/** \brief Find the closest pair on two finite segments.

    Both parameters are in `[0, 1]`; equal endpoints and parallel segments are
    valid. Published points have W one. Failure leaves \p output unchanged.
*/
int collision_segment_closest_points(const collision_segment_t *first,
                                     const collision_segment_t *second,
                                     collision_closest_pair_t *output);

/** \brief Project a point onto a unit plane.

    The plane normal must be unit length within a floating-point tolerance.
    The projected point has W one. Failure leaves \p output unchanged.
*/
int collision_point_plane_project(const point_t *point,
                                  const collision_plane_t *plane,
                                  collision_plane_projection_t *output);

/** \brief Find the closest point on a nondegenerate triangle.

    Published barycentric weights are inclusive and sum to one. The published
    point has W one. Failure leaves \p output unchanged.
*/
int collision_point_triangle_closest(
    const point_t *point, const collision_triangle_t *triangle,
    collision_triangle_closest_t *output);

/** \brief Intersect a ray with a nondegenerate, two-sided triangle.

    Triangle edges and vertices count as hits. The normal follows the supplied
    winding and has W zero. \p output may be NULL for a predicate-only query.
    A miss or failure leaves a non-NULL output unchanged.

    \retval 1  The ray intersects the triangle.
    \retval 0  The ray misses the triangle.
    \retval -1 Invalid or unrepresentable input; `errno` is set.
*/
int collision_ray_intersects_triangle(
    const collision_ray_t *ray, const collision_triangle_t *triangle,
    collision_triangle_hit_t *output);

/** \brief Intersect a ray with an axis-aligned box.

    A ray beginning inside the box reports an entry distance of zero. \p output
    may be NULL. A miss or failure leaves a non-NULL output unchanged.
*/
int collision_ray_intersects_aabb(const collision_ray_t *ray,
                                  const collision_aabb_t *box,
                                  collision_ray_interval_t *output);

/** \brief Intersect a ray with an oriented box. */
int collision_ray_intersects_obb(const collision_ray_t *ray,
                                 const collision_obb_t *box,
                                 collision_ray_interval_t *output);

/** \brief Test two spheres for inclusive overlap.

    \retval 1  The shapes overlap or touch.
    \retval 0  The shapes are separate.
    \retval -1 Invalid or unrepresentable input; `errno` is set.
*/
int collision_sphere_intersects_sphere(const collision_sphere_t *first,
                                       const collision_sphere_t *second);

/** \brief Test a sphere and capsule for inclusive overlap. */
int collision_sphere_intersects_capsule(const collision_sphere_t *sphere,
                                        const collision_capsule_t *capsule);

/** \brief Test two capsules for inclusive overlap. */
int collision_capsule_intersects_capsule(const collision_capsule_t *first,
                                         const collision_capsule_t *second);

/** \brief Test two axis-aligned boxes for inclusive overlap. */
int collision_aabb_intersects_aabb(const collision_aabb_t *first,
                                   const collision_aabb_t *second);

/** \brief Test an axis-aligned box and sphere for inclusive overlap. */
int collision_aabb_intersects_sphere(const collision_aabb_t *box,
                                     const collision_sphere_t *sphere);

/** \brief Test an oriented box and sphere for inclusive overlap. */
int collision_obb_intersects_sphere(const collision_obb_t *box,
                                    const collision_sphere_t *sphere);

/** \brief Test an oriented and axis-aligned box for inclusive overlap. */
int collision_obb_intersects_aabb(const collision_obb_t *oriented,
                                  const collision_aabb_t *aligned);

/** \brief Test two oriented boxes for inclusive overlap.

    The separating-axis query covers all three face axes from each box and all
    nine pairwise cross axes.
*/
int collision_obb_intersects_obb(const collision_obb_t *first,
                                 const collision_obb_t *second);

/** \brief Compute the axis-aligned bounds of a sphere.

    Published point W components are one. Failure leaves \p output unchanged.
*/
int collision_sphere_bounds(const collision_sphere_t *sphere,
                            collision_aabb_t *output);

/** \brief Compute the axis-aligned bounds of a capsule. */
int collision_capsule_bounds(const collision_capsule_t *capsule,
                             collision_aabb_t *output);

/** \brief Compute axis-aligned bounds for an oriented box. */
int collision_obb_bounds(const collision_obb_t *box,
                         collision_aabb_t *output);

/** \brief Compute bounds for a bounded, strided point stream.

    \p stride must be at least `sizeof(point_t)` and a multiple of four. The
    W component of every point is ignored. Pointer-range overflow is rejected
    before traversal. Failure leaves \p output unchanged.
*/
int collision_aabb_from_points(const void *points, size_t point_count,
                               size_t stride, collision_aabb_t *output);

/** @} */

__END_DECLS
#endif /* __DC_COLLISION_H */
