/* KallistiOS ##version##

   Checked caller-owned PVR particle example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define PARTICLE_CAPACITY 24u
#define TRAIL_VERTEX_CAPACITY ((PARTICLE_CAPACITY - 1u) * 6u)
#define TEXTURE_SIZE 16u

static alignas(32) const matrix_t screen_identity = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f }
};

static uint16_t texture_pixels[TEXTURE_SIZE * TEXTURE_SIZE];

static void build_texture(void) {
    size_t x;
    size_t y;

    for(y = 0; y < TEXTURE_SIZE; ++y) {
        for(x = 0; x < TEXTURE_SIZE; ++x) {
            int dx = (int)x - (int)(TEXTURE_SIZE / 2u);
            int dy = (int)y - (int)(TEXTURE_SIZE / 2u);
            unsigned int distance = (unsigned int)(dx * dx + dy * dy);

            texture_pixels[y * TEXTURE_SIZE + x] = distance < 36u ?
                UINT16_C(0xffff) : (distance < 64u ? UINT16_C(0xfda0) : 0);
        }
    }
}

static pvr_particle_t make_seed(unsigned int sequence) {
    static const uint32_t colors[] = {
        UINT32_C(0xffff6040), UINT32_C(0xffffff40),
        UINT32_C(0xff40ff80), UINT32_C(0xff40c0ff)
    };
    int horizontal = (int)(sequence % 9u) - 4;
    pvr_particle_t particle = {
        .position = { 320.0f, 400.0f, 0.55f, 1.0f },
        .velocity = { (float)horizontal * 14.0f,
                      -105.0f - (float)(sequence % 3u) * 8.0f,
                      0.0f, 0.0f },
        .acceleration = { 0.0f, 72.0f, 0.0f, 0.0f },
        .age = 0.0f,
        .lifetime = 1.5f,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .scale_velocity_x = -0.2f,
        .scale_velocity_y = -0.2f,
        .rotation = 0.0f,
        .angular_velocity = horizontal * 0.4f,
        .color = colors[sequence % (sizeof(colors) / sizeof(colors[0]))],
        .flags = PVR_PARTICLE_NONE,
        .cell_index = 0
    };

    return particle;
}

int main(int argc, char **argv) {
    const pvr_sprite_cell_t cell = {
        24.0f, 24.0f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f
    };
    const pvr_sprite_atlas_t atlas = { &cell, 1 };
    const pvr_particle_trail_desc_t trail_description = {
        3.0f, { 0.0f, 0.0f, 1.0f, 0.0f }, &screen_identity
    };
    pvr_particle_t particles[PARTICLE_CAPACITY];
    pvr_particle_stream_t stream = {
        particles, PARTICLE_CAPACITY, sizeof(particles[0])
    };
    pvr_sprite_instance_t instances[PARTICLE_CAPACITY];
    pvr_sprite_instance_stream_t instance_stream = {
        instances, 0, sizeof(instances[0])
    };
    alignas(32) pvr_sprite_txr_t sprites[PARTICLE_CAPACITY];
    alignas(32) pvr_vertex_t trail[TRAIL_VERTEX_CAPACITY];
    pvr_particle_step_result_t step_result;
    pvr_particle_emit_result_t instance_result;
    pvr_particle_emit_result_t trail_result;
    pvr_sprite_batch_result_t sprite_result;
    pvr_sprite_cxt_t sprite_context;
    pvr_poly_cxt_t trail_context;
    pvr_material_t sprite_material;
    pvr_material_t trail_material;
    pvr_pipeline_status_t status;
    pvr_ptr_t texture;
    unsigned int sequence = 0;
    unsigned int frame;

    (void)argc;
    (void)argv;

    assert(pvr_particle_pool_clear(&stream) == 0);
    vid_clear(96, 0, 0);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.08f);

    build_texture();
    texture = pvr_mem_malloc(sizeof(texture_pixels));
    assert(texture);
    assert(pvr_txr_load_ex_checked(texture_pixels, texture,
                                   TEXTURE_SIZE, TEXTURE_SIZE,
                                   PVR_TXRLOAD_16BPP) == 0);

    pvr_sprite_cxt_txr(&sprite_context, PVR_LIST_OP_POLY,
                       PVR_TXRFMT_RGB565, TEXTURE_SIZE, TEXTURE_SIZE,
                       texture, PVR_FILTER_BILINEAR);
    sprite_context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_material_compile_sprite(&sprite_material,
                                       &sprite_context, 0) == 0);

    pvr_poly_cxt_col(&trail_context, PVR_LIST_OP_POLY);
    trail_context.gen.culling = PVR_CULLING_NONE;
    trail_context.gen.shading = PVR_SHADE_GOURAUD;
    assert(pvr_material_compile_polygon(&trail_material,
                                        &trail_context, 0) == 0);

    for(frame = 0; frame < 180u; ++frame) {
        if(!(frame % 5u)) {
            pvr_particle_t seed = make_seed(sequence++);

            errno = 0;
            if(pvr_particle_spawn(&stream, &seed, NULL) < 0)
                assert(errno == ENOSPC);
        }
        assert(pvr_particle_step(&stream, 1.0f / 60.0f,
                                 &step_result) == 0);
        assert(step_result.examined_particles == PARTICLE_CAPACITY);

        assert(pvr_particle_emit_sprite_instances(
            instances, PARTICLE_CAPACITY, &stream, &instance_result) == 0);
        instance_stream.instance_count = instance_result.emitted_items;
        assert(pvr_sprite_batch_compile_2d(
            sprites, PARTICLE_CAPACITY, &atlas, &instance_stream,
            &sprite_result) == 0);
        assert(sprite_result.produced_sprites ==
               instance_result.emitted_items);

        assert(pvr_particle_compile_trail(
            trail, TRAIL_VERTEX_CAPACITY, &stream, &trail_description,
            &trail_result) == 0);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        if(sprite_result.produced_sprites) {
            pvr_geometry_vertex_sink_t sprite_sink;

            assert(pvr_material_submit(&sprite_material) == 0);
            assert(pvr_geometry_vertex_sink_init_current(
                &sprite_sink, PVR_GEOMETRY_VERTEX_SPRITE_TEXTURED) == 0);
            assert(pvr_geometry_vertex_sink_emit(
                &sprite_sink, sprites, sprite_result.produced_sprites) == 0);
        }
        if(trail_result.produced_vertices) {
            pvr_geometry_sink_t trail_sink;

            assert(pvr_material_submit(&trail_material) == 0);
            assert(pvr_geometry_sink_init_current(&trail_sink) == 0);
            assert(pvr_geometry_sink_emit(
                &trail_sink, trail, trail_result.produced_vertices) == 0);
        }
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
    }

    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&status) == 0);
    assert(status.faults.mask == PVR_FAULT_NONE);
    pvr_mem_free(texture);
    assert(pvr_shutdown() == 0);

    vid_clear(0, 64, 0);
    bfont_draw_str(vram_s + vid_mode->width * BFONT_HEIGHT +
                   BFONT_THIN_WIDTH * 2, vid_mode->width, 1,
                   "RESULT: PASS (caller-owned particles)");
    puts("RESULT: PASS (simulation + sprites + trails)");

    for(;;)
        thd_sleep(1000);
}
