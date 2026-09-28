/* KallistiOS ##version##
   Interactive renderer for the optional Compact animation showcase.
   Included after the shared scene loader and pose sampler.
   Copyright (C) 2026 Joseph Black
*/
#include "../showcase_common.h"
#include <dc/pvr_chunk_wire.h>

static pvr_poly_hdr_t showcase_surface;
static bool showcase_wire, showcase_paused;

static int showcase_begin(const pvr_chunk_cached_strip_t *strip, void *data) {
    (void)strip;
    (void)data;
    const pvr_poly_hdr_t *header = showcase_wire ? &demo_color_header : &showcase_surface;
    return pvr_prim(header, sizeof(*header));
}

static int showcase_shade(const pvr_chunk_render_state_t *state,
                           uint16_t index, const pvr_deform_vertex_t *vertex,
                           pvr_vertex_t *output, void *data) {
    static const pvr_light_t light = {
        .kind = PVR_LIGHT_DIRECTIONAL,
        .source.direction = { .x = -.36f, .y = .48f, .z = -.8f },
        .color = { .x = 1, .y = 1, .z = 1 }, .intensity = .8f
    };
    static const pvr_lighting_context_t lighting = { {.25f, .25f, .3f}, &light, 1 };
    const uint32_t color = output->argb;
    const pvr_lighting_sample_t input = {
        vertex->position, vertex->normal,
        { ((color >> 16) & 255u) / 255.0f, ((color >> 8) & 255u) / 255.0f,
          (color & 255u) / 255.0f, 1 }
    };
    const pvr_lighting_stream_t stream = { &input, 1, sizeof(input) };
    pvr_lighting_result_t result;
    (void)state;
    (void)index;
    (void)data;
    return pvr_lighting_apply(&output->argb, 1, &stream, &lighting, &result);
}

static void showcase_hud(void) {
    char status[43];
    snprintf(status, sizeof(status), "BEND %s  MORPH %s  %s  %s",
             showcase_bend ? "ON" : "OFF", showcase_morph ? "ON" : "OFF",
             showcase_wire ? "WIRE" : "LIT", showcase_paused ? "HOLD" : "PLAY");
    const char *lines[8] = {
        "COMPACT / IN MOTION",
        "Shared rig. Two deformation paths.",
        "01 / SKELETAL", "02 / SKIN + MORPH",
        "578 vertices / 2 joints per model",
        "A bend   B morph   X wireframe",
        "Y pause  START exit", status
    };
    demo_hud(lines);
}

/* A few asset-specific checks catch wiring mistakes without replacing the
   independent transform/normal goldens in chunk-skin-grid. */
static void showcase_check(void) {
    pvr_deform_vertex_t left, right, rest;
    assert(sample(0) == 0);
    assert(resolve(272, &rest, &app.model[0]) == 0);
    assert(sample(1) == 0);
    assert(resolve(272, &left, &app.model[0]) == 0);
    assert(fabsf(left.position.x - rest.position.x) > .2f);
    assert(resolve(136, &left, &app.model[0]) == 0);
    assert(resolve(136, &right, &app.model[1]) == 0);
    assert(fabsf(left.position.x - right.position.x) > .15f);
    showcase_morph = false;
    assert(sample(1) == 0);
    assert(resolve(136, &right, &app.model[1]) == 0);
    assert(fabsf(left.position.x - right.position.x) < .0001f);
    showcase_bend = false;
    assert(sample(1) == 0);
    assert(resolve(272, &left, &app.model[0]) == 0);
    assert(fabsf(left.position.x - rest.position.x) < .0001f);
    showcase_bend = showcase_morph = true;
    puts("KOSANIM asset_controls=PASS");
}

static int render(void) {
    pvr_geometry_sink_t sink;
    pvr_txr_surface_t texture = {0};
    alignas(32) uint16_t pixels[64 * 64];
    alignas(32) pvr_vertex_t vertices[STRIP_VERTICES];
    alignas(32) pvr_deform_vertex_t deformations[STRIP_VERTICES];
    pvr_chunk_wire_workspace_t wire_workspace = {vertices, deformations, STRIP_VERTICES};
    unsigned previous = 0, coverage = 0, frames = 0;
    size_t emitted = 0;
    float time = 0;

    showcase_check();
    demo_init();
    for(unsigned y = 0; y < 64; ++y)
        for(unsigned x = 0; x < 64; ++x)
            pixels[y * 64 + x] = (y % 8 < 2 || x % 16 == 0) ? 0x7bef : 0xffff;
    assert(pvr_txr_surface_alloc(&texture, 64, 64, PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    assert(pvr_txr_load_ex_checked(pixels, texture.vram, 64, 64, PVR_TXRLOAD_16BPP) == 0);
    pvr_poly_cxt_t context;
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, pvr_txr_surface_pvr_format(&texture),
                    64, 64, texture.vram, PVR_FILTER_BILINEAR);
    context.gen.culling = PVR_CULLING_NONE;
    context.txr.env = PVR_TXRENV_MODULATE;
    pvr_poly_compile(&showcase_surface, &context);
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    showcase_hud();
    for(;;) {
        bool changed = false;
        maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        const cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
        unsigned buttons = state ? state->buttons : 0;
        unsigned pressed = buttons & ~previous;
        previous = buttons;
        if(pressed & CONT_START) break;
        if(pressed & CONT_A) { showcase_bend = !showcase_bend; changed = true; }
        if(pressed & CONT_B) { showcase_morph = !showcase_morph; changed = true; }
        if(pressed & CONT_X) { showcase_wire = !showcase_wire; changed = true; }
        if(pressed & CONT_Y) { showcase_paused = !showcase_paused; changed = true; }
#ifdef CHUNK_SCENE_SHOWCASE_SMOKE
        if(frames == 180) break;
        if(frames % 20 == 0) {
            unsigned mode = frames / 20;
            showcase_bend = (mode & 1u) != 0;
            showcase_morph = (mode & 2u) != 0;
            showcase_wire = (mode & 4u) != 0;
            showcase_paused = mode == 8;
            changed = true;
        }
#endif
        if(changed) showcase_hud();
        if(!showcase_paused) {
            time += 1.0f / 120.0f;
            if(time >= 2) time -= 2;
        }
        assert(sample(time) == 0);
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_prim(&demo_color_header, sizeof(demo_color_header)) == 0);
        demo_quad(0, 0, 640, 480, .02f, 0xff101c30, 0xff050a14, 0, 0, 0, 0);
        for(unsigned i = 0; i < MODELS; ++i) {
            float x = 24 + i * 304;
            uint32_t accent = i ? 0xffffa052 : 0xff43d8f4;
            demo_quad(x, 103, 288, 279, .03f, 0xff17273c, 0xff0c1422, 0, 0, 0, 0);
            demo_quad(x, 103, 288, 3, .04f, accent, accent, 0, 0, 0, 0);
            for(unsigned line = 0; line < 6; ++line)
                demo_quad(x + 12, 139 + line * 38, 264, 1, .04f,
                          0xff1e3044, 0xff1e3044, 0, 0, 0, 0);
            demo_quad(x + 38, 351, 206, 4, .05f, accent, 0xff152436, 0, 0, 0, 0);
        }
        demo_quad(24, 391, 592, 3, .05f, 0xff24374e, 0xff24374e, 0, 0, 0, 0);
        demo_quad(24, 391, 592 * time / 2, 3, .06f, 0xff43d8f4, 0xffffa052, 0, 0, 0, 0);
        for(unsigned i = 0; i < MODELS; ++i) {
            shz_mat4x4_t view;
            alignas(32) matrix_t projection;
            shz_mat4x4_init_rotation_xyz(&view, -.18f, .4f, 0);
            for(unsigned col = 0; col < 4; ++col) {
                float depth = col == 3 ? 4 : view.elem2D[col][2];
                projection[col][0] = 360 * view.elem2D[col][0] + (148 + i * 304) * depth;
                projection[col][1] = -360 * view.elem2D[col][1] + 241 * depth;
                projection[col][2] = 0;
                projection[col][3] = depth;
            }
            if(showcase_wire) {
                pvr_frustum_t frustum;
                const pvr_chunk_wire_profile_t profile = {
                    .width = .8f, .argb = i ? 0xffffad62 : 0xff51dffa,
                    .topology = PVR_CHUNK_WIRE_MESH,
                    .color_mode = PVR_CHUNK_WIRE_COLOR_PROFILE
                };
                pvr_chunk_wire_result_t result;
                assert(pvr_frustum_init(&frustum, &projection, 0, 0, 640, 480, 1, 8) == 0);
                assert(pvr_chunk_model_cache_draw_emit_wire(&app.model[i].draw, &frustum,
                    PVR_CHUNK_CLIP_SPLIT, &profile, &sink, &wire_workspace, NULL,
                    showcase_begin, resolve, NULL, NULL, &app.model[i], &result) == 0);
                assert(result.emitted_edges > 0);
                emitted += result.emitted_vertices;
            }
            else {
                pvr_chunk_cache_result_t result;
                assert(pvr_chunk_model_cache_draw_emit(&app.model[i].draw, &projection,
                    &sink, vertices, STRIP_VERTICES, NULL, showcase_begin, resolve,
                    showcase_shade, &app.model[i], &result) == 0);
                assert(result.emitted_vertices == PACKETS && result.emitted_strips == STRIPS);
                emitted += result.emitted_vertices;
            }
        }
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        assert(pvr_prim(&demo_font_header, sizeof(demo_font_header)) == 0);
        demo_text(0, 24, 23, 1.05f, 0xffe4f6ff);
        demo_text(1, 26, 60, .65f, 0xff92adc5);
        demo_text(2, 38, 113, .65f, 0xff65e4fa);
        demo_text(3, 342, 113, .65f, 0xffffb477);
        demo_text(4, 26, 364, .5f, 0xffb2c4d4);
        demo_text(7, 26, 403, .6f, 0xffe4f6ff);
        demo_text(5, 26, 430, .6f, 0xff92adc5);
        demo_text(6, 26, 450, .6f, 0xff92adc5);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        coverage |= 1u << ((unsigned)showcase_bend | ((unsigned)showcase_morph << 1) |
                          ((unsigned)showcase_wire << 2));
        ++frames;
    }
    assert(pvr_wait_render_done() == 0);
    pvr_txr_surface_release(&texture);
    demo_finish();
#ifdef CHUNK_SCENE_SHOWCASE_SMOKE
    assert(coverage == 255 && showcase_paused);
#endif
    printf("KOSANIM frames=%u control_modes=%02x vertices=%u RESULT: PASS\n",
           frames, coverage, (unsigned)emitted);
    return 0;
}
