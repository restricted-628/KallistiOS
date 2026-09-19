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

static int finite_vector(const vector_t *vector) {
    return vector && isfinite(vector->x) && isfinite(vector->y) &&
           isfinite(vector->z);
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

static xyz_t cross(xyz_t lhs, xyz_t rhs) {
    xyz_t value = {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x
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

static int unit_xyz(xyz_t input, xyz_t *output) {
    float scale;
    float length_squared;
    float inverse_length;

    if(!output || !difference_valid(input)) {
        errno = EINVAL;
        return -1;
    }
    scale = fmaxf(fabsf(input.x), fmaxf(fabsf(input.y), fabsf(input.z)));
    if(scale == 0.0f) {
        errno = EDOM;
        return -1;
    }
    input.x /= scale;
    input.y /= scale;
    input.z /= scale;
    length_squared = magnitude_squared(input);
    if(!isfinite(length_squared) || length_squared <= FLT_MIN) {
        errno = ERANGE;
        return -1;
    }
#ifdef __DREAMCAST__
    inverse_length = frsqrt(length_squared);
#else
    inverse_length = 1.0f / sqrtf(length_squared);
#endif
    output->x = input.x * inverse_length;
    output->y = input.y * inverse_length;
    output->z = input.z * inverse_length;
    return 0;
}

static int ray_direction(const collision_ray_t *ray, xyz_t *direction) {
    xyz_t supplied;

    if(!ray || !finite_xyz(&ray->origin) || !finite_vector(&ray->direction)) {
        errno = EINVAL;
        return -1;
    }
    supplied = (xyz_t){ ray->direction.x, ray->direction.y,
                        ray->direction.z };
    return unit_xyz(supplied, direction);
}

static xyz_t scaled_subtract(const point_t *lhs, const point_t *rhs,
                             float scale);

static int triangle_basis(const collision_triangle_t *triangle, xyz_t *first,
                          xyz_t *second, xyz_t *normal, float *scale_output) {
    float scale;
    float normal_length;

    if(!triangle || !finite_xyz(&triangle->first) ||
       !finite_xyz(&triangle->second) || !finite_xyz(&triangle->third)) {
        errno = EINVAL;
        return -1;
    }
    scale = fmaxf(point_scale(&triangle->first),
                  fmaxf(point_scale(&triangle->second),
                        point_scale(&triangle->third)));
    if(scale == 0.0f) {
        errno = EDOM;
        return -1;
    }
    *first = scaled_subtract(&triangle->second, &triangle->first, scale);
    *second = scaled_subtract(&triangle->third, &triangle->first, scale);
    *normal = cross(*first, *second);
    normal_length = magnitude_squared(*normal);
    if(!isfinite(normal_length) || normal_length <= FLT_MIN) {
        errno = EDOM;
        return -1;
    }
    if(scale_output)
        *scale_output = scale;
    return 0;
}

static int obb_valid(const collision_obb_t *box) {
    xyz_t axes[3];
    int i;
    int j;

    if(!box || !finite_xyz(&box->center) ||
       !finite_vector(&box->half_extents) ||
       box->half_extents.x < 0.0f || box->half_extents.y < 0.0f ||
       box->half_extents.z < 0.0f)
        return 0;

    for(i = 0; i < 3; ++i) {
        float length;

        if(!finite_vector(&box->axes[i]))
            return 0;
        axes[i] = (xyz_t){ box->axes[i].x, box->axes[i].y,
                           box->axes[i].z };
        length = magnitude_squared(axes[i]);
        if(!isfinite(length) ||
           fabsf(length - 1.0f) > 64.0f * FLT_EPSILON)
            return 0;
    }
    for(i = 0; i < 3; ++i) {
        for(j = i + 1; j < 3; ++j) {
            if(fabsf(dot(axes[i], axes[j])) > 64.0f * FLT_EPSILON)
                return 0;
        }
    }
    return 1;
}

static float obb_extent(const collision_obb_t *box, int axis) {
    if(axis == 0)
        return box->half_extents.x;
    if(axis == 1)
        return box->half_extents.y;
    return box->half_extents.z;
}

static float axis_component(const vector_t *axis, int component) {
    if(component == 0)
        return axis->x;
    if(component == 1)
        return axis->y;
    return axis->z;
}

static point_t ray_point(const collision_ray_t *ray, xyz_t direction,
                         float distance) {
    point_t point = {
        ray->origin.x + direction.x * distance,
        ray->origin.y + direction.y * distance,
        ray->origin.z + direction.z * distance,
        1.0f
    };

    return point;
}

static collision_obb_t aabb_as_obb(const collision_aabb_t *box) {
    collision_obb_t oriented = {
        {
            box->minimum.x * 0.5f + box->maximum.x * 0.5f,
            box->minimum.y * 0.5f + box->maximum.y * 0.5f,
            box->minimum.z * 0.5f + box->maximum.z * 0.5f,
            1.0f
        },
        {
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f }
        },
        {
            box->maximum.x * 0.5f - box->minimum.x * 0.5f,
            box->maximum.y * 0.5f - box->minimum.y * 0.5f,
            box->maximum.z * 0.5f - box->minimum.z * 0.5f,
            0.0f
        }
    };

    return oriented;
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

int collision_point_triangle_closest(
    const point_t *point, const collision_triangle_t *triangle,
    collision_triangle_closest_t *output) {
    collision_triangle_closest_t result;
    xyz_t ab;
    xyz_t ac;
    xyz_t normal;
    xyz_t ap;
    xyz_t bp;
    xyz_t cp;
    float scale;
    float d1;
    float d2;
    float d3;
    float d4;
    float d5;
    float d6;
    float va;
    float vb;
    float vc;

    if(!output || !finite_xyz(point)) {
        errno = EINVAL;
        return -1;
    }
    if(triangle_basis(triangle, &ab, &ac, &normal, &scale) < 0)
        return -1;
    ap = scaled_subtract(point, &triangle->first, scale);
    bp = scaled_subtract(point, &triangle->second, scale);
    cp = scaled_subtract(point, &triangle->third, scale);
    if(!difference_valid(ap) || !difference_valid(bp) ||
       !difference_valid(cp)) {
        errno = ERANGE;
        return -1;
    }

    d1 = dot(ab, ap);
    d2 = dot(ac, ap);
    d3 = dot(ab, bp);
    d4 = dot(ac, bp);
    d5 = dot(ab, cp);
    d6 = dot(ac, cp);
    if(!isfinite(d1) || !isfinite(d2) || !isfinite(d3) || !isfinite(d4) ||
       !isfinite(d5) || !isfinite(d6)) {
        errno = ERANGE;
        return -1;
    }

    if(d1 <= 0.0f && d2 <= 0.0f) {
        result.first_weight = 1.0f;
        result.second_weight = 0.0f;
        result.third_weight = 0.0f;
    }
    else if(d3 >= 0.0f && d4 <= d3) {
        result.first_weight = 0.0f;
        result.second_weight = 1.0f;
        result.third_weight = 0.0f;
    }
    else {
        vc = d1 * d4 - d3 * d2;
        if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
            float denominator = d1 - d3;
            float weight;

            if(denominator <= 0.0f || !isfinite(denominator)) {
                errno = ERANGE;
                return -1;
            }
            weight = d1 / denominator;
            result.first_weight = 1.0f - weight;
            result.second_weight = weight;
            result.third_weight = 0.0f;
        }
        else if(d6 >= 0.0f && d5 <= d6) {
            result.first_weight = 0.0f;
            result.second_weight = 0.0f;
            result.third_weight = 1.0f;
        }
        else {
            vb = d5 * d2 - d1 * d6;
            if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
                float denominator = d2 - d6;
                float weight;

                if(denominator <= 0.0f || !isfinite(denominator)) {
                    errno = ERANGE;
                    return -1;
                }
                weight = d2 / denominator;
                result.first_weight = 1.0f - weight;
                result.second_weight = 0.0f;
                result.third_weight = weight;
            }
            else {
                va = d3 * d6 - d5 * d4;
                if(va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f) {
                    float first = d4 - d3;
                    float second = d5 - d6;
                    float denominator = first + second;
                    float weight;

                    if(denominator <= 0.0f || !isfinite(denominator)) {
                        errno = ERANGE;
                        return -1;
                    }
                    weight = first / denominator;
                    result.first_weight = 0.0f;
                    result.second_weight = 1.0f - weight;
                    result.third_weight = weight;
                }
                else {
                    float denominator = va + vb + vc;
                    float inverse;

                    if(denominator <= 0.0f || !isfinite(denominator)) {
                        errno = ERANGE;
                        return -1;
                    }
                    inverse = 1.0f / denominator;
                    result.second_weight = vb * inverse;
                    result.third_weight = vc * inverse;
                    result.first_weight = 1.0f - result.second_weight -
                                          result.third_weight;
                }
            }
        }
    }

    result.point = (point_t){
        triangle->first.x * result.first_weight +
            triangle->second.x * result.second_weight +
            triangle->third.x * result.third_weight,
        triangle->first.y * result.first_weight +
            triangle->second.y * result.second_weight +
            triangle->third.y * result.third_weight,
        triangle->first.z * result.first_weight +
            triangle->second.z * result.second_weight +
            triangle->third.z * result.third_weight,
        1.0f
    };
    if(!finite_xyz(&result.point) ||
       squared_distance(point, &result.point, &result.squared_distance) < 0)
        return -1;
    memcpy(output, &result, sizeof(result));
    return 0;
}

int collision_ray_intersects_triangle(
    const collision_ray_t *ray, const collision_triangle_t *triangle,
    collision_triangle_hit_t *output) {
    collision_triangle_hit_t result;
    xyz_t direction;
    xyz_t first_edge;
    xyz_t second_edge;
    xyz_t normal;
    xyz_t origin_delta;
    xyz_t direction_cross;
    xyz_t origin_cross;
    float scale;
    float determinant;
    float limit;
    float inverse;
    float second_weight;
    float third_weight;
    float scaled_distance;

    if(ray_direction(ray, &direction) < 0)
        return -1;
    if(triangle_basis(triangle, &first_edge, &second_edge, &normal,
                      &scale) < 0)
        return -1;
    origin_delta = scaled_subtract(&ray->origin, &triangle->first, scale);
    if(!difference_valid(origin_delta)) {
        errno = ERANGE;
        return -1;
    }
    direction_cross = cross(direction, second_edge);
    determinant = dot(first_edge, direction_cross);
    limit = 64.0f * FLT_EPSILON *
            sqrtf(magnitude_squared(first_edge) *
                  magnitude_squared(second_edge));
    if(!isfinite(determinant) || !isfinite(limit)) {
        errno = ERANGE;
        return -1;
    }
    if(fabsf(determinant) <= limit)
        return 0;

    inverse = 1.0f / determinant;
    second_weight = dot(origin_delta, direction_cross) * inverse;
    if(!isfinite(second_weight)) {
        errno = ERANGE;
        return -1;
    }
    if(second_weight < -64.0f * FLT_EPSILON ||
       second_weight > 1.0f + 64.0f * FLT_EPSILON)
        return 0;

    origin_cross = cross(origin_delta, first_edge);
    third_weight = dot(direction, origin_cross) * inverse;
    if(!isfinite(third_weight)) {
        errno = ERANGE;
        return -1;
    }
    if(third_weight < -64.0f * FLT_EPSILON ||
       second_weight + third_weight > 1.0f + 64.0f * FLT_EPSILON)
        return 0;

    scaled_distance = dot(second_edge, origin_cross) * inverse;
    if(!isfinite(scaled_distance)) {
        errno = ERANGE;
        return -1;
    }
    if(scaled_distance < -64.0f * FLT_EPSILON)
        return 0;
    second_weight = clamp01(second_weight);
    third_weight = clamp01(third_weight);
    if(second_weight + third_weight > 1.0f) {
        float total = second_weight + third_weight;

        second_weight /= total;
        third_weight /= total;
    }
    result.first_weight = 1.0f - second_weight - third_weight;
    result.second_weight = second_weight;
    result.third_weight = third_weight;
    result.distance = fmaxf(0.0f, scaled_distance) * scale;
    if(!isfinite(result.distance) || unit_xyz(normal, &normal) < 0) {
        errno = ERANGE;
        return -1;
    }
    result.point = (point_t){
        triangle->first.x * result.first_weight +
            triangle->second.x * result.second_weight +
            triangle->third.x * result.third_weight,
        triangle->first.y * result.first_weight +
            triangle->second.y * result.second_weight +
            triangle->third.y * result.third_weight,
        triangle->first.z * result.first_weight +
            triangle->second.z * result.second_weight +
            triangle->third.z * result.third_weight,
        1.0f
    };
    result.normal = (vector_t){ normal.x, normal.y, normal.z, 0.0f };
    if(!finite_xyz(&result.point)) {
        errno = ERANGE;
        return -1;
    }
    if(output)
        memcpy(output, &result, sizeof(result));
    return 1;
}

int collision_ray_intersects_obb(const collision_ray_t *ray,
                                 const collision_obb_t *box,
                                 collision_ray_interval_t *output) {
    collision_ray_interval_t result;
    xyz_t direction;
    xyz_t center_delta;
    float scale;
    float entry = 0.0f;
    float exit = FLT_MAX;
    int i;

    if(!obb_valid(box)) {
        errno = EINVAL;
        return -1;
    }
    if(ray_direction(ray, &direction) < 0)
        return -1;
    scale = geometry_scale(fmaxf(
        fmaxf(point_scale(&ray->origin), point_scale(&box->center)),
        fmaxf(box->half_extents.x,
              fmaxf(box->half_extents.y, box->half_extents.z))));
    center_delta = scaled_subtract(&ray->origin, &box->center, scale);
    if(!difference_valid(center_delta)) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < 3; ++i) {
        xyz_t axis = { box->axes[i].x, box->axes[i].y, box->axes[i].z };
        float origin = dot(center_delta, axis);
        float slope = dot(direction, axis);
        float extent = obb_extent(box, i) / scale;

        if(!isfinite(origin) || !isfinite(slope) || !isfinite(extent)) {
            errno = ERANGE;
            return -1;
        }
        if(fabsf(slope) <= 64.0f * FLT_EPSILON) {
            if(origin < -extent || origin > extent)
                return 0;
        }
        else {
            float first = (-extent - origin) / slope;
            float second = (extent - origin) / slope;

            if(first > second) {
                float temporary = first;
                first = second;
                second = temporary;
            }
            entry = fmaxf(entry, first);
            exit = fminf(exit, second);
            if(exit < entry)
                return 0;
        }
    }
    if(exit < 0.0f)
        return 0;
    result.entry_distance = entry * scale;
    result.exit_distance = exit * scale;
    if(!isfinite(result.entry_distance) || !isfinite(result.exit_distance)) {
        errno = ERANGE;
        return -1;
    }
    result.entry_point = ray_point(ray, direction, result.entry_distance);
    result.exit_point = ray_point(ray, direction, result.exit_distance);
    if(!finite_xyz(&result.entry_point) || !finite_xyz(&result.exit_point)) {
        errno = ERANGE;
        return -1;
    }
    if(output)
        memcpy(output, &result, sizeof(result));
    return 1;
}

int collision_ray_intersects_aabb(const collision_ray_t *ray,
                                  const collision_aabb_t *box,
                                  collision_ray_interval_t *output) {
    collision_obb_t oriented;

    if(!aabb_valid(box)) {
        errno = EINVAL;
        return -1;
    }
    oriented = aabb_as_obb(box);
    if(!obb_valid(&oriented)) {
        errno = ERANGE;
        return -1;
    }
    return collision_ray_intersects_obb(ray, &oriented, output);
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

int collision_obb_intersects_sphere(const collision_obb_t *box,
                                    const collision_sphere_t *sphere) {
    xyz_t center_delta;
    float scale;
    float radius;
    float squared_distance = 0.0f;
    int i;

    if(!obb_valid(box) || !sphere_valid(sphere)) {
        errno = EINVAL;
        return -1;
    }
    scale = geometry_scale(fmaxf(
        fmaxf(point_scale(&box->center), point_scale(&sphere->center)),
        fmaxf(sphere->radius,
              fmaxf(box->half_extents.x,
                    fmaxf(box->half_extents.y, box->half_extents.z)))));
    center_delta = scaled_subtract(&sphere->center, &box->center, scale);
    if(!difference_valid(center_delta)) {
        errno = ERANGE;
        return -1;
    }
    for(i = 0; i < 3; ++i) {
        xyz_t axis = { box->axes[i].x, box->axes[i].y, box->axes[i].z };
        float projection = fabsf(dot(center_delta, axis));
        float extent = obb_extent(box, i) / scale;
        float excess = fmaxf(projection - extent, 0.0f);

        squared_distance += excess * excess;
    }
    radius = sphere->radius / scale;
    if(!isfinite(squared_distance) || !isfinite(radius)) {
        errno = ERANGE;
        return -1;
    }
    return squared_distance <= radius * radius;
}

int collision_obb_intersects_obb(const collision_obb_t *first,
                                 const collision_obb_t *second) {
    xyz_t first_axes[3];
    xyz_t second_axes[3];
    xyz_t centers;
    float rotation[3][3];
    float absolute[3][3];
    float translation[3] = { 0.0f, 0.0f, 0.0f };
    float first_extents[3] = { 0.0f, 0.0f, 0.0f };
    float second_extents[3] = { 0.0f, 0.0f, 0.0f };
    float scale;
    int i;
    int j;

    if(!obb_valid(first) || !obb_valid(second)) {
        errno = EINVAL;
        return -1;
    }
    scale = geometry_scale(fmaxf(
        fmaxf(point_scale(&first->center), point_scale(&second->center)),
        fmaxf(fmaxf(first->half_extents.x, first->half_extents.y),
              fmaxf(first->half_extents.z,
                    fmaxf(fmaxf(second->half_extents.x,
                                second->half_extents.y),
                          second->half_extents.z)))));
    centers = scaled_subtract(&second->center, &first->center, scale);
    if(!difference_valid(centers)) {
        errno = ERANGE;
        return -1;
    }

    for(i = 0; i < 3; ++i) {
        first_axes[i] = (xyz_t){ first->axes[i].x, first->axes[i].y,
                                 first->axes[i].z };
        second_axes[i] = (xyz_t){ second->axes[i].x, second->axes[i].y,
                                  second->axes[i].z };
        first_extents[i] = obb_extent(first, i) / scale;
        second_extents[i] = obb_extent(second, i) / scale;
        translation[i] = dot(centers, first_axes[i]);
    }
    for(i = 0; i < 3; ++i) {
        for(j = 0; j < 3; ++j) {
            rotation[i][j] = dot(first_axes[i], second_axes[j]);
            absolute[i][j] = fabsf(rotation[i][j]) +
                             16.0f * FLT_EPSILON;
        }
    }

    /* Test both boxes' face normals, followed by the nine edge cross axes.
       Scaling the center delta and extents together keeps every separating
       comparison representable without changing its result. */
    for(i = 0; i < 3; ++i) {
        float second_radius = second_extents[0] * absolute[i][0] +
                              second_extents[1] * absolute[i][1] +
                              second_extents[2] * absolute[i][2];

        if(fabsf(translation[i]) > first_extents[i] + second_radius)
            return 0;
    }
    for(j = 0; j < 3; ++j) {
        float projected = fabsf(translation[0] * rotation[0][j] +
                                 translation[1] * rotation[1][j] +
                                 translation[2] * rotation[2][j]);
        float first_radius = first_extents[0] * absolute[0][j] +
                             first_extents[1] * absolute[1][j] +
                             first_extents[2] * absolute[2][j];

        if(projected > first_radius + second_extents[j])
            return 0;
    }
    for(i = 0; i < 3; ++i) {
        int first_next = (i + 1) % 3;
        int first_last = (i + 2) % 3;

        for(j = 0; j < 3; ++j) {
            int second_next = (j + 1) % 3;
            int second_last = (j + 2) % 3;
            float projected = fabsf(
                translation[first_last] * rotation[first_next][j] -
                translation[first_next] * rotation[first_last][j]);
            float first_radius =
                first_extents[first_next] * absolute[first_last][j] +
                first_extents[first_last] * absolute[first_next][j];
            float second_radius =
                second_extents[second_next] * absolute[i][second_last] +
                second_extents[second_last] * absolute[i][second_next];

            if(projected > first_radius + second_radius)
                return 0;
        }
    }
    return 1;
}

int collision_obb_intersects_aabb(const collision_obb_t *oriented,
                                  const collision_aabb_t *aligned) {
    collision_obb_t converted;

    if(!aabb_valid(aligned)) {
        errno = EINVAL;
        return -1;
    }
    converted = aabb_as_obb(aligned);
    if(!obb_valid(&converted)) {
        errno = ERANGE;
        return -1;
    }
    return collision_obb_intersects_obb(oriented, &converted);
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

int collision_obb_bounds(const collision_obb_t *box,
                         collision_aabb_t *output) {
    collision_aabb_t bounds;
    float scale;
    float extent[3];
    int component;
    int axis;

    if(!output || !obb_valid(box)) {
        errno = EINVAL;
        return -1;
    }
    scale = geometry_scale(fmaxf(box->half_extents.x,
                                 fmaxf(box->half_extents.y,
                                       box->half_extents.z)));
    for(component = 0; component < 3; ++component) {
        float sum = 0.0f;

        for(axis = 0; axis < 3; ++axis) {
            sum += fabsf(axis_component(&box->axes[axis], component)) *
                   (obb_extent(box, axis) / scale);
        }
        extent[component] = sum * scale;
        if(!isfinite(extent[component])) {
            errno = ERANGE;
            return -1;
        }
    }
    bounds.minimum = (point_t){ box->center.x - extent[0],
                                box->center.y - extent[1],
                                box->center.z - extent[2], 1.0f };
    bounds.maximum = (point_t){ box->center.x + extent[0],
                                box->center.y + extent[1],
                                box->center.z + extent[2], 1.0f };
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
