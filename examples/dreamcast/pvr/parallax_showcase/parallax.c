/* KallistiOS ##version##
   Neon harbor: caller-owned tile maps, cell animation and particles.
   Copyright (C) 2026 Joseph Black
*/
#include "../showcase_common.h"

KOS_INIT_FLAGS(INIT_DEFAULT);
#define COLS 32u
#define ROWS 8u
#define CAPACITY 96u
static uint32_t map_indices[3][COLS * ROWS];
static pvr_sprite_cell_t tile_cells[4], ship_cells[4];
static pvr_tilemap_tile_t tile_types[3][4];
static pvr_tilemap_t maps[3];
static alignas(32) pvr_vertex_t tile_vertices[8192];
static pvr_tilemap_draw_t tile_draws[1024];
static pvr_poly_hdr_t tile_header, ship_header, exhaust_header;
static pvr_ptr_t tile_texture, ship_texture, exhaust_texture;
static uint16_t tile_pixels[128 * 32], ship_pixels[128 * 32], glow_pixels[16 * 16];
static pvr_particle_t particles[CAPACITY];
static pvr_particle_stream_t pool = {particles, CAPACITY, sizeof(*particles)};
static alignas(32) pvr_vertex_t exhaust[6 * CAPACITY];
static alignas(32) const matrix_t screen = {
    {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}
};
static size_t tiles_drawn, sprites_drawn, particles_drawn;

static pvr_ptr_t upload(const uint16_t *pixels, unsigned w, unsigned h) {
    pvr_ptr_t t = pvr_mem_malloc(w * h * 2);
    assert(t);
    assert(pvr_txr_load_ex_checked(pixels, t, w, h, PVR_TXRLOAD_16BPP) == 0);
    return t;
}

static void build_scene(void) {
    /* Original procedural pixel artwork: four building/dock tiles. */
    for(unsigned y = 0; y < 32; ++y) for(unsigned x = 0; x < 128; ++x) {
        unsigned tile = x / 32, u = x % 32;
        uint16_t c = tile == 3 ? 0xf123 : 0xf235;
        if(u == 0 || u == 31) c = 0xf112;
        if(tile < 3 && u % 8 >= 2 && u % 8 <= 4 && y % 9 >= 2 && y % 9 <= 5)
            c = (u / 8 + y / 9 + tile) % 3 ? 0xf6cb : 0xf345;
        if(tile == 1 && y < 3) c = 0xfb68;
        if(tile == 2 && u > 23) c = y % 6 < 2 ? 0xffb6 : 0xf324;
        if(tile == 3) {
            if(y < 4) c = 0xf6dc;
            else if(y < 8) c = u % 12 < 6 ? 0xffc6 : 0xf345;
            else if(y % 16 == 0 || u % 16 == 0) c = 0xf456;
        }
        tile_pixels[y * 128 + x] = c;
    }
    /* Four exhaust/engine-light animation frames in one ship atlas. */
    for(unsigned f = 0; f < 4; ++f) for(unsigned y = 0; y < 32; ++y)
        for(unsigned x = 0; x < 32; ++x) {
            uint16_t c = 0;
            if(y >= 13 && y <= 20 && x >= 7 && x <= 27) c = 0xf9bc;
            if(y >= 14 && y <= 18 && x >= 27 && x <= 30) c = 0xffd8;
            if(y >= 9 && y <= 14 && x >= 13 && x <= 22) c = 0xf3ac;
            if(y >= 10 && y <= 12 && x >= 16 && x <= 22) c = 0xfaff;
            if(y >= 19 && y <= 23 && x >= 11 && x <= 21) c = 0xf567;
            if(y >= 10 && y <= 22 && x >= 6 && x <= 9) c = 0xfbc9;
            if(y >= 15 && y <= 18 && x >= 2 + f % 3 && x < 7) c = 0xff84;
            if(y == 14 && x >= 9 && x <= 27) c = 0xffdc;
            if(y == 20 && x >= 23 && x <= 25) c = f & 1 ? 0xff45 : 0xf723;
            ship_pixels[y * 128 + f * 32 + x] = c;
        }
    for(unsigned y = 0; y < 16; ++y) for(unsigned x = 0; x < 16; ++x) {
        int dx = (int)x - 8, dy = (int)y - 8;
        int a = 15 - (dx * dx + dy * dy) / 4;
        glow_pixels[y * 16 + x] = a > 0 ? (uint16_t)((a << 12) | 0x0fff) : 0;
    }
    const uint32_t tint[3] = {0xff536582, 0xffa5adb9, 0xffffffff};
    for(unsigned t = 0; t < 4; ++t) {
        tile_cells[t] = (pvr_sprite_cell_t){32,32,0,0,t * .25f,0,(t+1) * .25f,1};
        ship_cells[t] = (pvr_sprite_cell_t){64,64,.5f,.5f,t * .25f,0,(t+1) * .25f,1};
        for(unsigned layer = 0; layer < 3; ++layer)
            tile_types[layer][t] = (pvr_tilemap_tile_t){
                .atlas_cell_index=t, .list=PVR_LIST_OP_POLY,
                .argb={tint[layer],tint[layer],tint[layer],tint[layer]}
            };
    }
    for(unsigned layer = 0; layer < 3; ++layer) {
        for(unsigned y = 0; y < ROWS; ++y) for(unsigned x = 0; x < COLS; ++x) {
            unsigned height = layer == 0 ? 2 + (x * 7 / 3) % 6 :
                              layer == 1 ? 1 + (x / 3) % 4 : 1;
            map_indices[layer][y * COLS + x] = y >= ROWS - height ?
                (layer == 2 ? 3 : (x / 2) % 3) : PVR_TILEMAP_EMPTY;
        }
        maps[layer] = (pvr_tilemap_t){
            .indices=map_indices[layer], .index_count=COLS * ROWS,
            .columns=COLS, .rows=ROWS, .row_stride=COLS,
            .tiles=tile_types[layer], .tile_count=4,
            .atlas={tile_cells,4}, .tile_width=32, .tile_height=32
        };
    }
    tile_texture = upload(tile_pixels,128,32);
    ship_texture = upload(ship_pixels,128,32);
    exhaust_texture = upload(glow_pixels,16,16);
    pvr_poly_cxt_t c;
    pvr_poly_cxt_txr(&c,PVR_LIST_OP_POLY,PVR_TXRFMT_ARGB4444,128,32,tile_texture,PVR_FILTER_NONE);
    c.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&tile_header,&c);
    pvr_poly_cxt_txr(&c,PVR_LIST_PT_POLY,PVR_TXRFMT_ARGB4444,128,32,ship_texture,PVR_FILTER_NONE);
    c.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&ship_header,&c);
    pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444,16,16,exhaust_texture,PVR_FILTER_BILINEAR);
    c.gen.culling = PVR_CULLING_NONE;
    c.depth.write = PVR_DEPTHWRITE_DISABLE;
    c.blend.src = PVR_BLEND_SRCALPHA;
    c.blend.dst = PVR_BLEND_ONE;
    pvr_poly_compile(&exhaust_header,&c);
    assert(pvr_set_punch_through_alpha(128) == 0);
    assert(pvr_particle_pool_clear(&pool) == 0);
}

static void draw_maps(float scroll, bool parallax) {
    static const float speed[3] = {.23f,.58f,1};
    static const float scale[3] = {.8f,1,1.5f};
    assert(pvr_prim(&tile_header,sizeof(tile_header)) == 0);
    pvr_geometry_sink_t sink;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    for(unsigned layer = 0; layer < 3; ++layer) {
        pvr_tilemap_view_t view = {
            .left=24,.top=104,.right=616,.bottom=382,
            .anchor_x=24,.anchor_y=382 - ROWS * 32 * scale[layer],
            .scroll_x=scroll * (parallax ? speed[layer] : 1 / scale[layer]),
            .scale_x=scale[layer],.scale_y=scale[layer],
            .depth=layer == 2 ? 3 : .1f + layer * .1f,
            .address_x=PVR_TILEMAP_WRAP,.address_y=PVR_TILEMAP_CLIP,
            .max_candidates=1024
        };
        pvr_tilemap_result_t result;
        assert(pvr_tilemap_compile(tile_vertices,8192,tile_draws,1024,
                                   &maps[layer],&view,&result) == 0);
        assert(pvr_geometry_sink_emit(&sink,tile_vertices,result.vertex_count) == 0);
        tiles_drawn += result.visible_tiles;
    }
}

int main(void) {
    demo_init();
    build_scene();
    const char *lines[8] = {
        "NEON HARBOR / 2D LAB", "TILEMAPS + CELL ANIMATION + PARTICLES",
        "THREE DEPTH LAYERS / PROCEDURAL PIXEL ART",
        "A PARALLAX   B EXHAUST   X ANIMATION",
        "Y PAUSE / RESUME          START EXIT", NULL,NULL,NULL
    };
    demo_hud(lines);
    pvr_cell_key_t keys[4] = {0};
    for(unsigned i = 0; i < 4; ++i) {
        keys[i].time = .09f * i;
        keys[i].fields = PVR_CELL_KEY_ATLAS_CELL;
        keys[i].value.atlas_cell_index = i;
    }
    pvr_cell_stream_view_t streams[2];
    for(unsigned i = 0; i < 2; ++i) {
        pvr_cell_stream_t stream = {keys,4,.13f * i,.36f,1};
        assert(pvr_cell_stream_open(&stream,1,&streams[i]) == 0);
    }
    pvr_cell_state_t base = {.scale_x=1,.scale_y=1,
        .argb={0xffffffff,0xffffffff,0xffaabbdd,0xffaabbdd}};
    const pvr_sprite_atlas_t atlas = {ship_cells,4};
    const pvr_particle_billboard_desc_t billboard = {
        12,12,{{1,0,0,0},{0,1,0,0}},&screen
    };
    float time = 0, scroll = 0;
    unsigned frame = 0;
    bool parallax = true, emit = true, animation = true, pause = false;
#ifndef SHOWCASE_SMOKE
    uint32_t previous = 0;
#endif
    for(;;) {
#ifdef SHOWCASE_SMOKE
        if(frame == 180) break;
        parallax = frame < 60 || frame >= 120;
        animation = frame < 120;
#else
        maple_device_t *dev = maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
        cont_state_t *state = dev ? maple_dev_status(dev) : NULL;
        uint32_t buttons = state ? state->buttons : 0, pressed = buttons & ~previous;
        previous = buttons;
        if(pressed & CONT_START) break;
        if(pressed & CONT_A) parallax = !parallax;
        if(pressed & CONT_B) emit = !emit;
        if(pressed & CONT_X) animation = !animation;
        if(pressed & CONT_Y) pause = !pause;
#endif
        if(!pause) {
            time += 1.0f / 60;
            scroll += 1.2f;
            /* 300 map widths wrap every enabled/disabled layer phase. */
            if(scroll >= 307200) scroll -= 307200;
            assert(pvr_particle_step(&pool,1.0f/60,NULL) == 0);
            if(emit && frame % 2 == 0) {
                pvr_particle_t seed = {
                    .position={252,217 + 17 * shz_sinf(time * 1.8f),0,1},
                    .velocity={-110,-12 + (float)(frame % 7) * 4,0,0},
                    .lifetime=1.1f,.scale_x=1,.scale_y=.65f,
                    .scale_velocity_x=-.65f,.scale_velocity_y=-.35f,
                    .color=0xfffb9855
                };
                assert(pvr_particle_spawn(&pool,&seed,NULL) == 0);
            }
        }
        for(unsigned i = 0; i < CAPACITY; ++i)
            if(particles[i].flags & PVR_PARTICLE_ACTIVE) {
                unsigned alpha = (unsigned)(220 * (1 - particles[i].age / particles[i].lifetime));
                particles[i].color = (alpha << 24) | 0x00ff9955;
            }
        pvr_particle_emit_result_t particle_result;
        assert(pvr_particle_compile_billboards(exhaust,6 * CAPACITY,&pool,
                                               &billboard,&particle_result) == 0);
        alignas(32) pvr_vertex_t ships[8];
        for(unsigned i = 0; i < 2; ++i) {
            pvr_cell_state_t cell = base;
            assert(pvr_cell_stream_sample(&streams[i],animation ? time : 0,&cell,1,NULL) == 0);
            pvr_cell_sprite_t sprite = {
                .base_cells=&base,.cell_count=1,
                .position={i ? 440 : 280, i ? 177 + 8 * shz_sinf(time) :
                    220 + 17 * shz_sinf(time * 1.8f),i ? .6f : 2,1},
                .rotation=.06f * shz_sinf(time * 1.7f + i),
                .scale_x=i ? .6f : 1,.scale_y=i ? .6f : 1,
                .argb=i ? 0xffe2a9de : 0xffffffff,.oargb=0xffffffff
            };
            pvr_cell_resolved_t resolved;
            pvr_sprite_batch_result_t result;
            assert(pvr_cell_sprite_resolve(&sprite,&cell,1,&resolved,1,NULL) == 0);
            assert(pvr_cell_sprite_compile_colored_2d(ships+i*4,4,&atlas,
                                                      &resolved,1,&result) == 0);
            assert(result.produced_sprites == 1);
            ++sprites_drawn;
        }
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_prim(&demo_color_header,sizeof(demo_color_header)) == 0);
        demo_quad(24,104,592,278,.01f,0xff10182f,0xff594268,0,0,1,1);
        for(unsigned i = 0; i < 48; ++i) {
            float x = 32 + (i * 137) % 570, y = 116 + (i * 53) % 124;
            demo_quad(x,y,i%3 == 0 ? 2 : 1,2,.02f,0xff99b6cb,0xff99b6cb,0,0,1,1);
        }
        /* A pixel-art moon assembled from horizontal bands. */
        for(unsigned i = 0; i < 9; ++i) {
            unsigned inset = i < 4 ? 4-i : i-4;
            demo_quad(528 + inset * 2,126 + i * 4,36 - inset * 4,4,.03f,
                      0xffe7c3a4,0xffe7c3a4,0,0,1,1);
        }
        draw_maps(scroll,parallax);
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_PT_POLY) == 0);
        assert(pvr_prim(&ship_header,sizeof(ship_header)) == 0);
        assert(pvr_prim(ships,sizeof(ships)) == 0);
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        assert(pvr_prim(&exhaust_header,sizeof(exhaust_header)) == 0);
        if(particle_result.produced_vertices)
            assert(pvr_prim(exhaust,particle_result.produced_vertices * sizeof(*exhaust)) == 0);
        particles_drawn += particle_result.emitted_items;
        assert(pvr_prim(&demo_font_header,sizeof(demo_font_header)) == 0);
        demo_text(0,26,22,1,0xffe9f4ff);
        demo_text(1,27,56,.7f,0xff84a2bd);
        demo_text(2,26,394,.75f,0xffd5c3e6);
        demo_text(3,26,425,.75f,0xff9eb6cc);
        demo_text(4,26,450,.7f,0xff75d8c4);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        ++frame;
    }
    assert(pvr_wait_render_done() == 0);
    pvr_mem_free(tile_texture); pvr_mem_free(ship_texture); pvr_mem_free(exhaust_texture);
    demo_finish();
#ifdef SHOWCASE_SMOKE
    assert(tiles_drawn && sprites_drawn && particles_drawn);
    printf("RESULT: PASS (parallax showcase; %u frames, %zu tiles, %zu sprites, %zu particles)\n",
           frame,tiles_drawn,sprites_drawn,particles_drawn);
#else
    printf("Parallax showcase closed after %u frames\n",frame);
#endif
    return 0;
}
