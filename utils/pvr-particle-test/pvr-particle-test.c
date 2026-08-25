/* KallistiOS ##version##

   Host-side caller-owned PVR particle contract tests.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_particle.h>

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct extended_particle {
    pvr_particle_t particle;
    uint32_t application_data[4];
} extended_particle_t;

static alignas(32) const matrix_t identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

int pvr_prim(const void *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    (void)data;
    (void)size;
    return 0;
}

static int close_enough(float actual, float expected) {
    return isfinite(actual) && fabsf(actual - expected) <= 0.00001f;
}

static pvr_particle_t particle_seed(float x, float y, float z,
                                    float lifetime, uint32_t color) {
    pvr_particle_t particle = {
        .position = { x, y, z, 99.0f },
        .velocity = { 1.0f, 2.0f, 3.0f, 98.0f },
        .acceleration = { 2.0f, 0.0f, -2.0f, 97.0f },
        .age = 0.0f,
        .lifetime = lifetime,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .scale_velocity_x = 0.5f,
        .scale_velocity_y = -0.25f,
        .rotation = 0.0f,
        .angular_velocity = 2.0f,
        .color = color,
        .flags = PVR_PARTICLE_NONE,
        .cell_index = 3
    };

    return particle;
}

static void test_pool_and_spawn(void) {
    extended_particle_t slots[3];
    pvr_particle_stream_t stream = { slots, 3, sizeof(slots[0]) };
    pvr_particle_t seed = particle_seed(1.0f, 2.0f, 3.0f, 2.0f,
                                        UINT32_C(0xff102030));
    size_t index;
    size_t i;

    memset(slots, 0x5a, sizeof(slots));
    assert(pvr_particle_pool_clear(&stream) == 0);
    for(i = 0; i < 3u; ++i) {
        assert(slots[i].particle.flags == 0);
        assert(slots[i].particle.lifetime == 0.0f);
        assert(slots[i].application_data[0] == UINT32_C(0x5a5a5a5a));
    }

    seed.age = 1.0f;
    seed.flags = PVR_PARTICLE_FLIP_U;
    assert(pvr_particle_spawn(&stream, &seed, &index) == 0);
    assert(index == 0 && slots[0].particle.age == 0.0f);
    assert(slots[0].particle.flags ==
           (PVR_PARTICLE_ACTIVE | PVR_PARTICLE_FLIP_U));
    assert(slots[0].particle.position.w == 99.0f);
    assert(slots[0].application_data[0] == UINT32_C(0x5a5a5a5a));

    /* The seed may alias an active record; it is staged before publication. */
    assert(pvr_particle_spawn(&stream, &slots[0].particle, &index) == 0);
    assert(index == 1 && slots[1].particle.flags == slots[0].particle.flags);
    assert(pvr_particle_spawn(&stream, &seed, &index) == 0 && index == 2);

    index = 42;
    errno = 0;
    assert(pvr_particle_spawn(&stream, &seed, &index) == -1);
    assert(errno == ENOSPC && index == SIZE_MAX);

    slots[2].particle.flags = UINT32_C(0x80000000);
    slots[1].particle.flags = 0;
    errno = 0;
    assert(pvr_particle_spawn(&stream, &seed, &index) == -1);
    assert(errno == EINVAL && slots[1].particle.flags == 0);
}

static void test_step(void) {
    pvr_particle_t particles[3] = {
        particle_seed(0.0f, 0.0f, 1.0f, 2.0f,
                      UINT32_C(0xff000001)),
        particle_seed(5.0f, 0.0f, 1.0f, 0.25f,
                      UINT32_C(0xff000002)),
        { 0 }
    };
    pvr_particle_stream_t stream = {
        particles, 3, sizeof(particles[0])
    };
    pvr_particle_step_result_t result;
    pvr_particle_t unchanged[3];

    particles[0].flags = PVR_PARTICLE_ACTIVE;
    particles[1].flags = PVR_PARTICLE_ACTIVE;
    assert(pvr_particle_step(&stream, 0.5f, &result) == 0);
    assert(result.examined_particles == 3 && result.active_before == 2 &&
           result.active_after == 1 && result.expired_particles == 1);
    assert(close_enough(particles[0].position.x, 0.75f));
    assert(close_enough(particles[0].position.y, 1.0f));
    assert(close_enough(particles[0].position.z, 2.25f));
    assert(close_enough(particles[0].velocity.x, 2.0f));
    assert(close_enough(particles[0].velocity.z, 2.0f));
    assert(close_enough(particles[0].scale_x, 1.25f));
    assert(close_enough(particles[0].scale_y, 0.875f));
    assert(close_enough(particles[0].rotation, 1.0f));
    assert(close_enough(particles[0].age, 0.5f));
    assert(particles[0].position.w == 99.0f);
    assert(!(particles[1].flags & PVR_PARTICLE_ACTIVE));
    assert(particles[1].age == particles[1].lifetime);
    assert(close_enough(particles[1].position.x, 5.3125f));

    particles[0].velocity.x = FLT_MAX;
    memcpy(unchanged, particles, sizeof(particles));
    errno = 0;
    assert(pvr_particle_step(&stream, 2.0f, &result) == -1);
    assert(errno == ERANGE);
    assert(!memcmp(particles, unchanged, sizeof(particles)));
    particles[0].velocity.x = 2.0f;

    errno = 0;
    assert(pvr_particle_step(&stream, -1.0f, &result) == -1);
    assert(errno == EINVAL && result.examined_particles == 0);
}

static void test_sprite_instances(void) {
    pvr_particle_t particles[4] = {
        particle_seed(1.0f, 2.0f, 3.0f, 2.0f,
                      UINT32_C(0xff000001)),
        particle_seed(4.0f, 5.0f, 6.0f, 2.0f,
                      UINT32_C(0xff000002)),
        particle_seed(7.0f, 8.0f, 9.0f, 2.0f,
                      UINT32_C(0xff000003)),
        { 0 }
    };
    pvr_particle_stream_t stream = {
        particles, 4, sizeof(particles[0])
    };
    pvr_sprite_instance_t output[3];
    pvr_sprite_instance_t unchanged[3];
    pvr_particle_emit_result_t result;

    particles[0].flags = PVR_PARTICLE_ACTIVE | PVR_PARTICLE_FLIP_U;
    particles[0].rotation = 0.25f;
    particles[0].scale_x = 2.0f;
    particles[1].flags = PVR_PARTICLE_ACTIVE | PVR_PARTICLE_HIDDEN;
    particles[2].flags = PVR_PARTICLE_ACTIVE | PVR_PARTICLE_FLIP_V;
    particles[2].cell_index = 9;

    assert(pvr_particle_emit_sprite_instances(output, 3, &stream,
                                               &result) == 0);
    assert(result.examined_particles == 4 && result.emitted_items == 2 &&
           result.produced_vertices == 0);
    assert(output[0].cell_index == 3 && output[0].position.x == 1.0f &&
           output[0].scale_x == 2.0f && output[0].rotation == 0.25f);
    assert(output[0].flags == PVR_SPRITE_INSTANCE_FLIP_U);
    assert(output[1].cell_index == 9 &&
           output[1].flags == PVR_SPRITE_INSTANCE_FLIP_V);

    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_particle_emit_sprite_instances(output, 1, &stream,
                                               &result) == -1);
    assert(errno == ENOSPC && !memcmp(output, unchanged, sizeof(output)));

    errno = 0;
    assert(pvr_particle_emit_sprite_instances(
        (pvr_sprite_instance_t *)(void *)particles, 3, &stream,
        &result) == -1);
    assert(errno == EINVAL);

    particles[0].flags = 0;
    particles[1].flags = 0;
    particles[2].flags = 0;
    assert(pvr_particle_emit_sprite_instances(NULL, 0, &stream,
                                               &result) == 0);
    assert(result.examined_particles == 4 && result.emitted_items == 0);
}

static void test_billboards(void) {
    pvr_particle_t particles[2] = {
        particle_seed(10.0f, 20.0f, 3.0f, 2.0f,
                      UINT32_C(0xff123456)),
        particle_seed(30.0f, 40.0f, 3.0f, 2.0f,
                      UINT32_C(0xffabcdef))
    };
    pvr_particle_stream_t stream = {
        particles, 2, sizeof(particles[0])
    };
    const pvr_particle_billboard_desc_t description = {
        4.0f, 2.0f,
        {
            { 1.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f }
        },
        &identity
    };
    alignas(32) pvr_vertex_t output[12];
    alignas(32) pvr_vertex_t unchanged[12];
    pvr_particle_emit_result_t result;
    alignas(32) matrix_t behind;

    particles[0].flags = PVR_PARTICLE_ACTIVE;
    particles[0].scale_x = particles[0].scale_y = 1.0f;
    particles[0].rotation = 0.0f;
    particles[1].flags = PVR_PARTICLE_ACTIVE | PVR_PARTICLE_HIDDEN;
    assert(pvr_particle_compile_billboards(output, 12, &stream,
                                            &description, &result) == 0);
    assert(result.examined_particles == 2 && result.emitted_items == 1 &&
           result.produced_vertices == 6);
    assert(output[0].x == 8.0f && output[0].y == 21.0f &&
           output[0].z == 1.0f);
    assert(output[1].x == 8.0f && output[1].y == 19.0f);
    assert(output[2].x == 12.0f && output[2].y == 19.0f &&
           output[2].flags == PVR_CMD_VERTEX_EOL);
    assert(output[5].x == 12.0f && output[5].y == 21.0f &&
           output[5].flags == PVR_CMD_VERTEX_EOL);
    assert(output[0].u == 0.0f && output[0].v == 1.0f);
    assert(output[2].u == 1.0f && output[2].v == 0.0f);
    assert(output[0].argb == particles[0].color);

    memcpy(unchanged, output, sizeof(output));
    errno = 0;
    assert(pvr_particle_compile_billboards(output, 5, &stream,
                                            &description, &result) == -1);
    assert(errno == ENOSPC && !memcmp(output, unchanged, sizeof(output)));

    memcpy(behind, identity, sizeof(behind));
    behind[3][3] = -1.0f;
    {
        pvr_particle_billboard_desc_t invalid = description;
        invalid.world_to_screen = &behind;
        errno = 0;
        assert(pvr_particle_compile_billboards(output, 12, &stream,
                                                &invalid, &result) == -1);
        assert(errno == ERANGE && !memcmp(output, unchanged, sizeof(output)));
    }

    particles[0].flags = 0;
    assert(pvr_particle_compile_billboards(NULL, 0, &stream,
                                            &description, &result) == 0);
    assert(result.produced_vertices == 0 && result.emitted_items == 0);
}

static void test_trails(void) {
    pvr_particle_t particles[3] = {
        particle_seed(0.0f, 0.0f, 2.0f, 2.0f,
                      UINT32_C(0xffff0000)),
        particle_seed(2.0f, 0.0f, 2.0f, 2.0f,
                      UINT32_C(0xff00ff00)),
        particle_seed(4.0f, 0.0f, 2.0f, 2.0f,
                      UINT32_C(0xff0000ff))
    };
    pvr_particle_stream_t stream = {
        particles, 3, sizeof(particles[0])
    };
    const pvr_particle_trail_desc_t description = {
        2.0f, { 0.0f, 0.0f, 1.0f, 0.0f }, &identity
    };
    alignas(32) pvr_vertex_t output[12];
    alignas(32) pvr_vertex_t unchanged[12];
    pvr_particle_emit_result_t result;
    size_t i;

    for(i = 0; i < 3u; ++i) {
        particles[i].flags = PVR_PARTICLE_ACTIVE;
        particles[i].scale_x = 1.0f;
    }
    assert(pvr_particle_compile_trail(output, 12, &stream, &description,
                                      &result) == 0);
    assert(result.examined_particles == 3 && result.emitted_items == 2 &&
           result.produced_vertices == 12);
    assert(output[0].x == 0.0f && output[0].y == -1.0f);
    assert(output[1].x == 2.0f && output[1].y == -1.0f);
    assert(output[2].x == 2.0f && output[2].y == 1.0f);
    assert(output[5].x == 0.0f && output[5].y == 1.0f);
    assert(output[0].argb == particles[0].color);
    assert(output[1].argb == particles[1].color);
    assert(output[2].flags == PVR_CMD_VERTEX_EOL);

    particles[1].flags |= PVR_PARTICLE_HIDDEN;
    assert(pvr_particle_compile_trail(NULL, 0, &stream, &description,
                                      &result) == 0);
    assert(result.emitted_items == 0 && result.produced_vertices == 0);
    particles[1].flags &= ~PVR_PARTICLE_HIDDEN;

    particles[1].position = particles[0].position;
    assert(pvr_particle_compile_trail(output, 6, &stream, &description,
                                      &result) == 0);
    assert(result.emitted_items == 1 && result.produced_vertices == 6);

    memcpy(unchanged, output, sizeof(output));
    particles[0].position.x = -FLT_MAX;
    particles[1].position.x = FLT_MAX;
    errno = 0;
    assert(pvr_particle_compile_trail(output, 12, &stream, &description,
                                      &result) == -1);
    assert(errno == ERANGE && !memcmp(output, unchanged, sizeof(output)));
}

int main(void) {
    test_pool_and_spawn();
    test_step();
    test_sprite_instances();
    test_billboards();
    test_trails();
    puts("PVR particle tests passed");
    return 0;
}
