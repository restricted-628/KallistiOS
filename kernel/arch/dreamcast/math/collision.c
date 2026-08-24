/* KallistiOS ##version##

   collision.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/collision.h>

#ifdef __DREAMCAST__
#include <dc/fmath.h>
#endif

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct xyz {
    float x;
    float y;
    float z;
} xyz_t;

static int finite_xyz(const point_t *point) {
    return point && isfinite(point->x) && isfinite(point->y) &&
           isfinite(point->z);
}

static int finite_radius(float radius) {
    return isfinite(radius) && radius >= 0.0f;
}

static float point_scale(const point_t *point) {
    return fmaxf(fabsf(point->x),
                 fmaxf(fabsf(point->y), fabsf(point->z)));
}

static xyz_t subtract(const point_t *lhs, const point_t *rhs) {
    xyz_t value = {
        lhs->x - rhs->x,
        lhs->y - rhs->y,
        lhs->z - rhs->z
    };

    return value;
}

static float dot(xyz_t lhs, xyz_t rhs) {
#ifdef __DREAMCAST__
    return fipr(lhs.x, lhs.y, lhs.z, 0.0f,
                rhs.x, rhs.y, rhs.z, 0.0f);
#else
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
#endif
}

static float magnitude_squared(xyz_t value) {
#ifdef __DREAMCAST__
    return fipr_magnitude_sqr(value.x, value.y, value.z, 0.0f);
#else
    return dot(value, value);
#endif
}

static float clamp01(float value) {
    if(value <= 0.0f)
        return 0.0f;
    if(value >= 1.0f)
        return 1.0f;
    return value;
}

static int difference_valid(xyz_t value) {
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static int segment_valid(const collision_segment_t *segment) {
    return segment && finite_xyz(&segment->start) && finite_xyz(&segment->end);
}

static int capsule_valid(const collision_capsule_t *capsule) {
    return capsule && finite_xyz(&capsule->start) &&
           finite_xyz(&capsule->end) && finite_radius(capsule->radius);
}

static int sphere_valid(const collision_sphere_t *sphere) {
    return sphere && finite_xyz(&sphere->center) &&
           finite_radius(sphere->radius);
}

static int aabb_valid(const collision_aabb_t *box) {
    return box && finite_xyz(&box->minimum) && finite_xyz(&box->maximum) &&
           box->minimum.x <= box->maximum.x &&
           box->minimum.y <= box->maximum.y &&
           box->minimum.z <= box->maximum.z;
}

static point_t segment_point(const collision_segment_t *segment,
                             float parameter) {
    float inverse = 1.0f - parameter;
    point_t point = {
        segment->start.x * inverse + segment->end.x * parameter,
        segment->start.y * inverse + segment->end.y * parameter,
        segment->start.z * inverse + segment->end.z * parameter,
        1.0f
    };

    return point;
}

static float geometry_scale(float scale) {
    return scale > 0.0f ? scale : 1.0f;
}

static xyz_t scaled_subtract(const point_t *lhs, const point_t *rhs,
                             float scale) {
    xyz_t value = {
        lhs->x / scale - rhs->x / scale,
        lhs->y / scale - rhs->y / scale,
        lhs->z / scale - rhs->z / scale
    };

    return value;
}

static int point_segment_parameter(const point_t *point,
                                   const collision_segment_t *segment,
                                   float *output) {
    float scale = geometry_scale(fmaxf(point_scale(point),
                                       fmaxf(point_scale(&segment->start),
                                             point_scale(&segment->end))));
    xyz_t direction = scaled_subtract(&segment->end, &segment->start, scale);
    xyz_t from_start = scaled_subtract(point, &segment->start, scale);
    float denominator = magnitude_squared(direction);

    if(!isfinite(denominator) || denominator < 0.0f) {
        errno = ERANGE;
        return -1;
    }
    if(denominator <= FLT_MIN)
        *output = 0.0f;
    else {
        float numerator = dot(from_start, direction);

        if(!isfinite(numerator)) {
            errno = ERANGE;
            return -1;
        }
        *output = clamp01(numerator / denominator);
    }
    return 0;
}

static int segment_parameters(const collision_segment_t *first,
                              const collision_segment_t *second,
                              float *first_output, float *second_output) {
    float scale = geometry_scale(
        fmaxf(fmaxf(point_scale(&first->start), point_scale(&first->end)),
              fmaxf(point_scale(&second->start), point_scale(&second->end))));
    xyz_t first_direction = scaled_subtract(&first->end, &first->start, scale);
    xyz_t second_direction = scaled_subtract(&second->end, &second->start,
                                             scale);
    xyz_t origins = scaled_subtract(&first->start, &second->start, scale);
    float first_length = magnitude_squared(first_direction);
    float second_length = magnitude_squared(second_direction);
    float relation = dot(first_direction, second_direction);
    float first_projection = dot(first_direction, origins);
    float second_projection = dot(second_direction, origins);
    float first_parameter;
    float second_parameter;

    if(!isfinite(first_length) || !isfinite(second_length) ||
       !isfinite(relation) || !isfinite(first_projection) ||
       !isfinite(second_projection) || first_length < 0.0f ||
       second_length < 0.0f) {
        errno = ERANGE;
        return -1;
    }

    /* Degenerate segments become points. Otherwise solve the unconstrained
       pair, clamp the first segment, then clamp and repair the second. */
    if(first_length <= FLT_MIN && second_length <= FLT_MIN) {
        first_parameter = 0.0f;
        second_parameter = 0.0f;
    }
    else if(first_length <= FLT_MIN) {
        first_parameter = 0.0f;
        second_parameter = clamp01(second_projection / second_length);
    }
    else if(second_length <= FLT_MIN) {
        second_parameter = 0.0f;
        first_parameter = clamp01(-first_projection / first_length);
    }
    else {
        float denominator = first_length * second_length -
                            relation * relation;

        if(!isfinite(denominator)) {
            errno = ERANGE;
            return -1;
        }
        if(denominator > FLT_EPSILON * first_length * second_length)
            first_parameter = clamp01((relation * second_projection -
                                       first_projection * second_length) /
                                      denominator);
        else
            first_parameter = 0.0f;

        second_parameter = (relation * first_parameter +
                            second_projection) / second_length;
        if(second_parameter < 0.0f) {
            second_parameter = 0.0f;
            first_parameter = clamp01(-first_projection / first_length);
        }
        else if(second_parameter > 1.0f) {
            second_parameter = 1.0f;
            first_parameter = clamp01((relation - first_projection) /
                                      first_length);
        }
    }

    *first_output = first_parameter;
    *second_output = second_parameter;
    return 0;
}

static int squared_distance(const point_t *first, const point_t *second,
                            float *output) {
    xyz_t delta = subtract(first, second);
    float distance;

    if(!difference_valid(delta)) {
        errno = ERANGE;
        return -1;
    }
    distance = magnitude_squared(delta);
    if(!isfinite(distance) || distance < 0.0f) {
        errno = ERANGE;
        return -1;
    }
    *output = distance;
    return 0;
}

static int point_distance_within_radii(const point_t *first,
                                       const point_t *second,
                                       float first_radius,
                                       float second_radius) {
    float scale = fmaxf(fmaxf(point_scale(first), point_scale(second)),
                        fmaxf(first_radius, second_radius));
    xyz_t delta;
    float radius;

    if(scale == 0.0f)
        return 1;
    delta = scaled_subtract(first, second, scale);
    radius = first_radius / scale + second_radius / scale;
    return magnitude_squared(delta) <= radius * radius;
}

int collision_plane_from_points(const point_t *first, const point_t *second,
                                const point_t *third,
                                collision_plane_t *output) {
    collision_plane_t plane;
    xyz_t first_edge;
    xyz_t second_edge;
    xyz_t normal;
    float scale;
    float length_squared;
    float inverse_length;

    if(!output || !finite_xyz(first) || !finite_xyz(second) ||
       !finite_xyz(third)) {
        errno = EINVAL;
        return -1;
    }

    /* Scaling both edges by one common value preserves the cross-product
       direction while preventing valid large coordinates from overflowing. */
    scale = fmaxf(point_scale(first),
                  fmaxf(point_scale(second), point_scale(third)));
    if(scale == 0.0f) {
        errno = ERANGE;
        return -1;
    }
    first_edge = scaled_subtract(second, first, scale);
    second_edge = scaled_subtract(third, first, scale);

    normal.x = first_edge.y * second_edge.z -
               first_edge.z * second_edge.y;
    normal.y = first_edge.z * second_edge.x -
               first_edge.x * second_edge.z;
    normal.z = first_edge.x * second_edge.y -
               first_edge.y * second_edge.x;
    length_squared = magnitude_squared(normal);
    if(!isfinite(length_squared) || length_squared <= FLT_MIN) {
        errno = ERANGE;
        return -1;
    }

#ifdef __DREAMCAST__
    inverse_length = frsqrt(length_squared);
#else
    inverse_length = 1.0f / sqrtf(length_squared);
#endif
    normal.x *= inverse_length;
    normal.y *= inverse_length;
    normal.z *= inverse_length;
    plane.normal = (vector_t){ normal.x, normal.y, normal.z, 0.0f };
    plane.offset = -(normal.x * first->x + normal.y * first->y +
                     normal.z * first->z);
    if(!isfinite(plane.offset)) {
        errno = ERANGE;
        return -1;
    }

    memcpy(output, &plane, sizeof(plane));
    return 0;
}

int collision_point_segment_closest(const point_t *point,
                                    const collision_segment_t *segment,
                                    collision_closest_point_t *output) {
    collision_closest_point_t result;
    if(!output || !finite_xyz(point) || !segment_valid(segment)) {
        errno = EINVAL;
        return -1;
    }
    if(point_segment_parameter(point, segment, &result.parameter) < 0)
        return -1;
    result.point = segment_point(segment, result.parameter);
    if(!finite_xyz(&result.point) ||
       squared_distance(point, &result.point, &result.squared_distance) < 0)
        return -1;

    memcpy(output, &result, sizeof(result));
    return 0;
}

int collision_segment_closest_points(const collision_segment_t *first,
                                     const collision_segment_t *second,
                                     collision_closest_pair_t *output) {
    collision_closest_pair_t result;
    float first_parameter;
    float second_parameter;

    if(!output || !segment_valid(first) || !segment_valid(second)) {
        errno = EINVAL;
        return -1;
    }
    if(segment_parameters(first, second, &first_parameter,
                          &second_parameter) < 0)
        return -1;

    result.first_parameter = first_parameter;
    result.second_parameter = second_parameter;
    result.first_point = segment_point(first, first_parameter);
    result.second_point = segment_point(second, second_parameter);
    if(!finite_xyz(&result.first_point) || !finite_xyz(&result.second_point) ||
       squared_distance(&result.first_point, &result.second_point,
                        &result.squared_distance) < 0)
        return -1;

    memcpy(output, &result, sizeof(result));
    return 0;
}

int collision_point_plane_project(const point_t *point,
                                  const collision_plane_t *plane,
                                  collision_plane_projection_t *output) {
    collision_plane_projection_t result;
    xyz_t normal;
    float normal_length;

    if(!output || !finite_xyz(point) || !plane ||
       !finite_xyz(&plane->normal) || !isfinite(plane->offset)) {
        errno = EINVAL;
        return -1;
    }
    normal = (xyz_t){ plane->normal.x, plane->normal.y, plane->normal.z };
    normal_length = magnitude_squared(normal);
    if(!isfinite(normal_length) ||
       fabsf(normal_length - 1.0f) > 32.0f * FLT_EPSILON) {
        errno = EDOM;
        return -1;
    }

    result.signed_distance = normal.x * point->x + normal.y * point->y +
                             normal.z * point->z + plane->offset;
    result.point = (point_t){
        point->x - normal.x * result.signed_distance,
        point->y - normal.y * result.signed_distance,
        point->z - normal.z * result.signed_distance,
        1.0f
    };
    if(!isfinite(result.signed_distance) || !finite_xyz(&result.point)) {
        errno = ERANGE;
        return -1;
    }
    memcpy(output, &result, sizeof(result));
    return 0;
}

int collision_sphere_intersects_sphere(const collision_sphere_t *first,
                                       const collision_sphere_t *second) {
    if(!sphere_valid(first) || !sphere_valid(second)) {
        errno = EINVAL;
        return -1;
    }
    return point_distance_within_radii(&first->center, &second->center,
                                       first->radius, second->radius);
}

int collision_sphere_intersects_capsule(const collision_sphere_t *sphere,
                                        const collision_capsule_t *capsule) {
    collision_segment_t axis;
    point_t closest;
    float parameter;

    if(!sphere_valid(sphere) || !capsule_valid(capsule)) {
        errno = EINVAL;
        return -1;
    }
    axis = (collision_segment_t){ capsule->start, capsule->end };
    if(point_segment_parameter(&sphere->center, &axis, &parameter) < 0)
        return -1;
    closest = segment_point(&axis, parameter);
    if(!finite_xyz(&closest)) {
        errno = ERANGE;
        return -1;
    }
    return point_distance_within_radii(&sphere->center, &closest,
                                       sphere->radius, capsule->radius);
}

int collision_capsule_intersects_capsule(const collision_capsule_t *first,
                                         const collision_capsule_t *second) {
    collision_segment_t first_axis;
    collision_segment_t second_axis;
    point_t first_point;
    point_t second_point;
    float first_parameter;
    float second_parameter;

    if(!capsule_valid(first) || !capsule_valid(second)) {
        errno = EINVAL;
        return -1;
    }
    first_axis = (collision_segment_t){ first->start, first->end };
    second_axis = (collision_segment_t){ second->start, second->end };
    if(segment_parameters(&first_axis, &second_axis, &first_parameter,
                          &second_parameter) < 0)
        return -1;
    first_point = segment_point(&first_axis, first_parameter);
    second_point = segment_point(&second_axis, second_parameter);
    if(!finite_xyz(&first_point) || !finite_xyz(&second_point)) {
        errno = ERANGE;
        return -1;
    }
    return point_distance_within_radii(&first_point, &second_point,
                                       first->radius, second->radius);
}

int collision_aabb_intersects_aabb(const collision_aabb_t *first,
                                   const collision_aabb_t *second) {
    if(!aabb_valid(first) || !aabb_valid(second)) {
        errno = EINVAL;
        return -1;
    }
    return first->minimum.x <= second->maximum.x &&
           first->maximum.x >= second->minimum.x &&
           first->minimum.y <= second->maximum.y &&
           first->maximum.y >= second->minimum.y &&
           first->minimum.z <= second->maximum.z &&
           first->maximum.z >= second->minimum.z;
}

int collision_aabb_intersects_sphere(const collision_aabb_t *box,
                                     const collision_sphere_t *sphere) {
    point_t closest;
    if(!aabb_valid(box) || !sphere_valid(sphere)) {
        errno = EINVAL;
        return -1;
    }
    closest = (point_t){
        fminf(fmaxf(sphere->center.x, box->minimum.x), box->maximum.x),
        fminf(fmaxf(sphere->center.y, box->minimum.y), box->maximum.y),
        fminf(fmaxf(sphere->center.z, box->minimum.z), box->maximum.z),
        1.0f
    };
    return point_distance_within_radii(&sphere->center, &closest,
                                       sphere->radius, 0.0f);
}

int collision_sphere_bounds(const collision_sphere_t *sphere,
                            collision_aabb_t *output) {
    collision_aabb_t bounds;

    if(!output || !sphere_valid(sphere)) {
        errno = EINVAL;
        return -1;
    }
    bounds.minimum = (point_t){ sphere->center.x - sphere->radius,
                                sphere->center.y - sphere->radius,
                                sphere->center.z - sphere->radius, 1.0f };
    bounds.maximum = (point_t){ sphere->center.x + sphere->radius,
                                sphere->center.y + sphere->radius,
                                sphere->center.z + sphere->radius, 1.0f };
    if(!finite_xyz(&bounds.minimum) || !finite_xyz(&bounds.maximum)) {
        errno = ERANGE;
        return -1;
    }
    memcpy(output, &bounds, sizeof(bounds));
    return 0;
}

int collision_capsule_bounds(const collision_capsule_t *capsule,
                             collision_aabb_t *output) {
    collision_aabb_t bounds;

    if(!output || !capsule_valid(capsule)) {
        errno = EINVAL;
        return -1;
    }
    bounds.minimum = (point_t){
        fminf(capsule->start.x, capsule->end.x) - capsule->radius,
        fminf(capsule->start.y, capsule->end.y) - capsule->radius,
        fminf(capsule->start.z, capsule->end.z) - capsule->radius,
        1.0f
    };
    bounds.maximum = (point_t){
        fmaxf(capsule->start.x, capsule->end.x) + capsule->radius,
        fmaxf(capsule->start.y, capsule->end.y) + capsule->radius,
        fmaxf(capsule->start.z, capsule->end.z) + capsule->radius,
        1.0f
    };
    if(!finite_xyz(&bounds.minimum) || !finite_xyz(&bounds.maximum)) {
        errno = ERANGE;
        return -1;
    }
    memcpy(output, &bounds, sizeof(bounds));
    return 0;
}

int collision_aabb_from_points(const void *points, size_t point_count,
                               size_t stride, collision_aabb_t *output) {
    collision_aabb_t bounds;
    const uint8_t *cursor = points;
    size_t i;

    if(!points || !point_count || !output || stride < sizeof(point_t) ||
       (stride & 3u)) {
        errno = EINVAL;
        return -1;
    }
    if(point_count - 1u > (SIZE_MAX - sizeof(point_t)) / stride ||
       (point_count - 1u) * stride + sizeof(point_t) >
       UINTPTR_MAX - (uintptr_t)points) {
        errno = ERANGE;
        return -1;
    }

    memcpy(&bounds.minimum, cursor, sizeof(point_t));
    if(!finite_xyz(&bounds.minimum)) {
        errno = EDOM;
        return -1;
    }
    bounds.maximum = bounds.minimum;
    for(i = 1; i < point_count; ++i) {
        point_t point;

        cursor += stride;
        memcpy(&point, cursor, sizeof(point));
        if(!finite_xyz(&point)) {
            errno = EDOM;
            return -1;
        }
        bounds.minimum.x = fminf(bounds.minimum.x, point.x);
        bounds.minimum.y = fminf(bounds.minimum.y, point.y);
        bounds.minimum.z = fminf(bounds.minimum.z, point.z);
        bounds.maximum.x = fmaxf(bounds.maximum.x, point.x);
        bounds.maximum.y = fmaxf(bounds.maximum.y, point.y);
        bounds.maximum.z = fmaxf(bounds.maximum.z, point.z);
    }
    bounds.minimum.w = 1.0f;
    bounds.maximum.w = 1.0f;
    memcpy(output, &bounds, sizeof(bounds));
    return 0;
}
