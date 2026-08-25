/* KallistiOS ##version##

   Collision geometry example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static int close_enough(float actual, float expected) {
    return fabsf(actual - expected) < 0.0001f;
}

int main(int argc, char **argv) {
    const float diagonal = 0.7071067811865475f;
    const collision_capsule_t guard = {
        { -2.0f, 0.0f, 0.0f, 1.0f },
        { 2.0f, 0.0f, 0.0f, 1.0f },
        0.5f
    };
    collision_sphere_t actor = { { 0.0f, 2.0f, 0.0f, 1.0f }, 1.5f };
    collision_segment_t path = {
        { -3.0f, 2.0f, 0.0f, 1.0f },
        { 3.0f, -2.0f, 0.0f, 1.0f }
    };
    collision_segment_t axis = { guard.start, guard.end };
    collision_closest_pair_t closest;
    collision_aabb_t bounds;
    collision_plane_t plane;
    collision_plane_projection_t projection;
    const point_t plane_a = { 0.0f, 0.0f, 0.0f, 1.0f };
    const point_t plane_b = { 1.0f, 0.0f, 0.0f, 1.0f };
    const point_t plane_c = { 0.0f, 1.0f, 1.0f, 1.0f };
    const point_t above_plane = { 0.0f, 0.0f, 2.0f, 1.0f };
    const collision_triangle_t floor_triangle = {
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 4.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 4.0f, 0.0f, 1.0f }
    };
    const collision_ray_t pick_ray = {
        { 1.0f, 1.0f, 3.0f, 1.0f },
        { 0.0f, 0.0f, -4.0f, 0.0f }
    };
    collision_triangle_hit_t floor_hit;
    collision_obb_t rotated = {
        { 0.0f, 0.0f, 0.0f, 1.0f },
        {
            { diagonal, diagonal, 0.0f, 0.0f },
            { -diagonal, diagonal, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f, 0.0f }
        },
        { 2.0f, 1.0f, 1.0f, 0.0f }
    };

    (void)argc;
    (void)argv;

    /* Contact is inclusive: the 1.5-radius sphere touches the capsule. */
    assert(collision_sphere_intersects_capsule(&actor, &guard) == 1);
    actor.center.y = 2.01f;
    assert(collision_sphere_intersects_capsule(&actor, &guard) == 0);

    assert(collision_segment_closest_points(&path, &axis, &closest) == 0);
    assert(close_enough(closest.squared_distance, 0.0f));
    assert(closest.first_point.w == 1.0f);
    assert(closest.second_point.w == 1.0f);

    assert(collision_capsule_bounds(&guard, &bounds) == 0);
    assert(close_enough(bounds.minimum.x, -2.5f));
    assert(close_enough(bounds.maximum.x, 2.5f));
    assert(close_enough(bounds.minimum.y, -0.5f));
    assert(close_enough(bounds.maximum.y, 0.5f));

    /* Exercise the optimized normalization path with a non-axis plane. */
    assert(collision_plane_from_points(&plane_a, &plane_b, &plane_c,
                                       &plane) == 0);
    assert(collision_point_plane_project(&above_plane, &plane,
                                         &projection) == 0);
    assert(close_enough(projection.point.y, 1.0f));
    assert(close_enough(projection.point.z, 1.0f));

    /* The direction is deliberately non-unit; distance is still world-space. */
    assert(collision_ray_intersects_triangle(&pick_ray, &floor_triangle,
                                              &floor_hit) == 1);
    assert(close_enough(floor_hit.distance, 3.0f));
    assert(close_enough(floor_hit.first_weight, 0.5f));
    assert(close_enough(floor_hit.second_weight, 0.25f));
    assert(close_enough(floor_hit.third_weight, 0.25f));

    actor.center = (point_t){ 3.0f * diagonal, 3.0f * diagonal,
                              0.0f, 1.0f };
    actor.radius = 1.0f;
    assert(collision_obb_intersects_sphere(&rotated, &actor) == 1);
    assert(collision_obb_bounds(&rotated, &bounds) == 0);
    assert(close_enough(bounds.maximum.x, 3.0f * diagonal));
    assert(close_enough(bounds.maximum.y, 3.0f * diagonal));

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2,
                   vid_mode->width, 1,
                   "RESULT: PASS (collision geometry)");
    puts("RESULT: PASS (collision geometry)");

    for(;;)
        thd_sleep(1000);
}
