/* KallistiOS ##version##

   collision-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/collision.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOLERANCE 0.00002f

static int near(float actual, float expected) {
    return fabsf(actual - expected) <= TOLERANCE;
}

static void check_point(point_t point, float x, float y, float z) {
    assert(near(point.x, x));
    assert(near(point.y, y));
    assert(near(point.z, z));
    assert(point.w == 1.0f);
}

static void test_plane(void) {
    const point_t a = { 0.0f, 2.0f, 0.0f, 99.0f };
    const point_t b = { 1.0f, 2.0f, 0.0f, -12.0f };
    const point_t c = { 0.0f, 2.0f, -1.0f, 4.0f };
    const point_t point = { 3.0f, 5.0f, 7.0f, -2.0f };
    collision_plane_t plane;
    collision_plane_projection_t projection;
    collision_plane_t sentinel;

    assert(collision_plane_from_points(&a, &b, &c, &plane) == 0);
    assert(near(plane.normal.x, 0.0f));
    assert(near(plane.normal.y, 1.0f));
    assert(near(plane.normal.z, 0.0f));
    assert(plane.normal.w == 0.0f);
    assert(near(plane.offset, -2.0f));

    assert(collision_point_plane_project(&point, &plane, &projection) == 0);
    assert(near(projection.signed_distance, 3.0f));
    check_point(projection.point, 3.0f, 2.0f, 7.0f);

    memset(&sentinel, 0x5a, sizeof(sentinel));
    plane = sentinel;
    errno = 0;
    assert(collision_plane_from_points(&a, &a, &c, &plane) == -1);
    assert(errno == ERANGE);
    assert(memcmp(&plane, &sentinel, sizeof(plane)) == 0);

    plane = (collision_plane_t){ { 0.0f, 2.0f, 0.0f, 0.0f }, -4.0f };
    errno = 0;
    assert(collision_point_plane_project(&point, &plane, &projection) == -1);
    assert(errno == EDOM);
}

static void test_point_segment(void) {
    const collision_segment_t segment = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 10.0f, 0.0f, 0.0f, 0.0f }
    };
    collision_closest_point_t result;
    point_t point = { 4.0f, 3.0f, 0.0f, 42.0f };

    assert(collision_point_segment_closest(&point, &segment, &result) == 0);
    assert(near(result.parameter, 0.4f));
    assert(near(result.squared_distance, 9.0f));
    check_point(result.point, 4.0f, 0.0f, 0.0f);

    point.x = -3.0f;
    assert(collision_point_segment_closest(&point, &segment, &result) == 0);
    assert(result.parameter == 0.0f);
    check_point(result.point, 0.0f, 0.0f, 0.0f);

    point.x = 13.0f;
    assert(collision_point_segment_closest(&point, &segment, &result) == 0);
    assert(result.parameter == 1.0f);
    check_point(result.point, 10.0f, 0.0f, 0.0f);

    {
        const collision_segment_t point_segment = {
            { 2.0f, 2.0f, 2.0f, 0.0f },
            { 2.0f, 2.0f, 2.0f, 0.0f }
        };
        const point_t origin = { 0.0f, 0.0f, 0.0f, 0.0f };

        assert(collision_point_segment_closest(&origin, &point_segment,
                                               &result) == 0);
        assert(result.parameter == 0.0f);
        assert(near(result.squared_distance, 12.0f));
    }
}

static void test_segment_pair(void) {
    collision_closest_pair_t result;
    const collision_segment_t crossing_a = {
        { -2.0f, 0.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f, 0.0f }
    };
    const collision_segment_t crossing_b = {
        { 0.0f, -2.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f, 0.0f }
    };
    const collision_segment_t parallel = {
        { -2.0f, 3.0f, 0.0f, 0.0f }, { 2.0f, 3.0f, 0.0f, 0.0f }
    };
    const collision_segment_t point_a = {
        { 5.0f, 2.0f, 0.0f, 0.0f }, { 5.0f, 2.0f, 0.0f, 0.0f }
    };

    assert(collision_segment_closest_points(&crossing_a, &crossing_b,
                                            &result) == 0);
    assert(near(result.first_parameter, 0.5f));
    assert(near(result.second_parameter, 0.5f));
    assert(near(result.squared_distance, 0.0f));

    assert(collision_segment_closest_points(&crossing_a, &parallel,
                                            &result) == 0);
    assert(near(result.squared_distance, 9.0f));

    assert(collision_segment_closest_points(&point_a, &crossing_a,
                                            &result) == 0);
    assert(result.first_parameter == 0.0f);
    assert(result.second_parameter == 1.0f);
    check_point(result.second_point, 2.0f, 0.0f, 0.0f);
    assert(near(result.squared_distance, 13.0f));
}

static void test_round_overlap(void) {
    collision_sphere_t sphere_a = { { 0.0f, 0.0f, 0.0f, 0.0f }, 2.0f };
    collision_sphere_t sphere_b = { { 4.0f, 0.0f, 0.0f, 0.0f }, 2.0f };
    collision_capsule_t capsule_a = {
        { 5.0f, -1.0f, 0.0f, 0.0f },
        { 5.0f, 1.0f, 0.0f, 0.0f },
        1.0f
    };
    collision_capsule_t capsule_b = {
        { 0.0f, 2.0f, 0.0f, 0.0f },
        { 10.0f, 2.0f, 0.0f, 0.0f },
        1.0f
    };

    assert(collision_sphere_intersects_sphere(&sphere_a, &sphere_b) == 1);
    sphere_b.center.x = 4.01f;
    assert(collision_sphere_intersects_sphere(&sphere_a, &sphere_b) == 0);

    sphere_a.center.x = 3.0f;
    sphere_a.radius = 1.0f;
    assert(collision_sphere_intersects_capsule(&sphere_a, &capsule_a) == 1);
    sphere_a.center.x = 2.9f;
    assert(collision_sphere_intersects_capsule(&sphere_a, &capsule_a) == 0);

    assert(collision_capsule_intersects_capsule(&capsule_a, &capsule_b) == 1);
    capsule_b.start.y = 3.01f;
    capsule_b.end.y = 3.01f;
    assert(collision_capsule_intersects_capsule(&capsule_a, &capsule_b) == 0);

    sphere_a.radius = -1.0f;
    errno = 0;
    assert(collision_sphere_intersects_capsule(&sphere_a, &capsule_a) == -1);
    assert(errno == EINVAL);

    /* Boolean queries remain defined when a naive subtraction or radius
       square would overflow even though every shape component is finite. */
    sphere_a = (collision_sphere_t){
        { -FLT_MAX, 0.0f, 0.0f, 0.0f }, FLT_MAX
    };
    sphere_b = (collision_sphere_t){
        { FLT_MAX, 0.0f, 0.0f, 0.0f }, FLT_MAX
    };
    assert(collision_sphere_intersects_sphere(&sphere_a, &sphere_b) == 1);
    sphere_a.radius = FLT_MAX * 0.49f;
    sphere_b.radius = FLT_MAX * 0.49f;
    assert(collision_sphere_intersects_sphere(&sphere_a, &sphere_b) == 0);
}

static void test_large_coordinates(void) {
    const collision_segment_t segment = {
        { -FLT_MAX, 0.0f, 0.0f, 0.0f },
        { FLT_MAX, 0.0f, 0.0f, 0.0f }
    };
    const point_t origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    collision_closest_point_t closest;

    assert(collision_point_segment_closest(&origin, &segment, &closest) == 0);
    assert(near(closest.parameter, 0.5f));
    check_point(closest.point, 0.0f, 0.0f, 0.0f);
    assert(closest.squared_distance == 0.0f);
}

static void test_boxes_and_bounds(void) {
    collision_aabb_t box = {
        { -1.0f, -1.0f, -1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 0.0f }
    };
    collision_aabb_t other = {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 2.0f, 1.0f, 1.0f, 0.0f }
    };
    collision_sphere_t sphere = { { 2.0f, 0.0f, 0.0f, 0.0f }, 1.0f };
    collision_capsule_t capsule = {
        { -2.0f, 4.0f, 1.0f, 0.0f },
        { 3.0f, -1.0f, 5.0f, 0.0f },
        0.5f
    };

    assert(collision_aabb_intersects_aabb(&box, &other) == 1);
    other.minimum.x = 1.01f;
    assert(collision_aabb_intersects_aabb(&box, &other) == 0);
    assert(collision_aabb_intersects_sphere(&box, &sphere) == 1);
    sphere.center.x = 2.01f;
    assert(collision_aabb_intersects_sphere(&box, &sphere) == 0);

    sphere = (collision_sphere_t){ { 2.0f, 3.0f, 4.0f, 0.0f }, 2.0f };
    assert(collision_sphere_bounds(&sphere, &box) == 0);
    check_point(box.minimum, 0.0f, 1.0f, 2.0f);
    check_point(box.maximum, 4.0f, 5.0f, 6.0f);

    assert(collision_capsule_bounds(&capsule, &box) == 0);
    check_point(box.minimum, -2.5f, -1.5f, 0.5f);
    check_point(box.maximum, 3.5f, 4.5f, 5.5f);
}

static void test_point_stream(void) {
    struct vertex {
        point_t point;
        unsigned int tag;
    } points[] = {
        { { 4.0f, -2.0f, 9.0f, 77.0f }, 1 },
        { { -3.0f, 8.0f, 2.0f, -1.0f }, 2 },
        { { 1.0f, 0.0f, -5.0f, 4.0f }, 3 }
    };
    collision_aabb_t bounds;
    collision_aabb_t sentinel;

    assert(collision_aabb_from_points(points, 3, sizeof(points[0]),
                                      &bounds) == 0);
    check_point(bounds.minimum, -3.0f, -2.0f, -5.0f);
    check_point(bounds.maximum, 4.0f, 8.0f, 9.0f);

    memset(&sentinel, 0xa5, sizeof(sentinel));
    bounds = sentinel;
    points[1].point.y = NAN;
    errno = 0;
    assert(collision_aabb_from_points(points, 3, sizeof(points[0]),
                                      &bounds) == -1);
    assert(errno == EDOM);
    assert(memcmp(&bounds, &sentinel, sizeof(bounds)) == 0);

    errno = 0;
    assert(collision_aabb_from_points(points, 0, sizeof(points[0]),
                                      &bounds) == -1);
    assert(errno == EINVAL);
}

int main(void) {
    test_plane();
    test_point_segment();
    test_segment_pair();
    test_round_overlap();
    test_large_coordinates();
    test_boxes_and_bounds();
    test_point_stream();
    puts("collision-test: PASS");
    return 0;
}
