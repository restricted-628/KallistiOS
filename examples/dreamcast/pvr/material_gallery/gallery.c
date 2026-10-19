/* KallistiOS ##version##
   Opaque material gallery, using the existing PVR material recipes.
   Copyright (C) 2026 Joseph Black
*/
#include "../showcase_common.h"

KOS_INIT_FLAGS(INIT_DEFAULT);
#define SEGMENTS 24u
#define RINGS 12u
#define VERTICES (SEGMENTS * RINGS * 6u)
#define TEXSIZE 128u
enum { ENVIRONMENT, BUMP, LIGHTMAP, EMISSIVE, MATERIAL_COUNT };
typedef struct model {
    pvr_vertex_t source[VERTICES];
    vector_t normals[VERTICES];
    pvr_vertex_t projected[VERTICES];
    pvr_vertex_t layer[VERTICES];
    size_t count;
} model_t;
static alignas(32) model_t models[MATERIAL_COUNT];
static vector_t transformed_normals[VERTICES];
static pvr_lighting_sample_t samples[VERTICES];
static uint32_t lit_colors[VERTICES];
static uint16_t pixels[6][TEXSIZE * TEXSIZE];
static pvr_ptr_t textures[6];
static pvr_material_t surfaces[MATERIAL_COUNT], environment;
static pvr_material_recipe_t recipes[3];
static size_t projected_count, completed_layers;

static float unit(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static uint16_t rgb(float r, float g, float b) {
    return ((unsigned)(unit(r) * 31) << 11) |
           ((unsigned)(unit(g) * 63) << 5) | (unsigned)(unit(b) * 31);
}

static void build_textures(void) {
    for(unsigned y = 0; y < TEXSIZE; ++y) for(unsigned x = 0; x < TEXSIZE; ++x) {
        float u = (float)x / TEXSIZE, v = (float)y / TEXSIZE;
        size_t p = y * TEXSIZE + x;
        /* A painted studio environment, not a live reflection capture. */
        float dx = u - .32f, dy = v - .23f;
        float lamp = 1 / (1 + 190 * (dx * dx + dy * dy));
        float horizon = v > .5f ? .8f : .25f;
        float stripe = (x > 85 && x < 94 && y < 80) ? .5f : 0;
        pixels[0][p] = rgb(.08f + horizon * v + lamp + stripe,
                           .16f + .22f * (1-v) + lamp + stripe,
                           .23f + .4f * (1-v) + lamp);
        bool line = x % 32 < 2 || y % 32 < 2;
        float panel = ((x / 32) ^ (y / 32)) & 1 ? .8f : 1;
        pixels[1][p] = line ? rgb(.05f,.09f,.12f) :
                             rgb(.32f * panel,.57f * panel,.65f * panel);
        /* Tangent-space normal angles from a repeating sinusoidal height. */
        float nx = -.85f * shz_cosf(u * SHZ_F_TAU * 4);
        float ny = -.85f * shz_cosf(v * SHZ_F_TAU * 4);
        float elevation = shz_atan2f(1,shz_sqrtf(nx * nx + ny * ny));
        float azimuth = shz_atan2f(ny,nx);
        if(azimuth < 0) azimuth += SHZ_F_TAU;
        unsigned s = (unsigned)(unit(elevation / (SHZ_F_PI * .5f)) * 255);
        unsigned r = (unsigned)(unit(azimuth / SHZ_F_TAU) * 255);
        pixels[2][p] = (s << 8) | r;
        /* Authored UV-space illumination: warm pools and grille shadows. */
        float pool = .25f + .7f * unit(1 - 4 * (u-.5f) * (u-.5f));
        float slat = y % 24 < 6 ? .3f : 1;
        pixels[3][p] = rgb(pool * slat,pool * slat * .85f,pool * slat * .55f);
        bool circuit = x % 32 < 3 || (y % 32 < 3 && x % 64 < 40);
        bool node = x % 32 >= 12 && x % 32 <= 18 && y % 32 >= 12 && y % 32 <= 18;
        pixels[4][p] = circuit ? rgb(.04f,.8f,.58f) : node ? rgb(.85f,.26f,.08f) : 0;
        unsigned brickx = (x + (y / 16 % 2) * 16) % 32;
        pixels[5][p] = (brickx < 2 || y % 16 < 2) ? rgb(.14f,.12f,.1f) :
            rgb(.8f,.51f,.27f);
    }
    for(unsigned i = 0; i < 6; ++i) {
        textures[i] = pvr_mem_malloc(sizeof(pixels[i]));
        assert(textures[i]);
        assert(pvr_txr_load_ex_checked(pixels[i],textures[i],TEXSIZE,TEXSIZE,
                                        PVR_TXRLOAD_16BPP) == 0);
    }
}

static void make_context(pvr_poly_cxt_t *c, unsigned texture, uint32_t format) {
    pvr_poly_cxt_txr(c,PVR_LIST_OP_POLY,format,TEXSIZE,TEXSIZE,
                    textures[texture],PVR_FILTER_BILINEAR);
    c->gen.culling = PVR_CULLING_CCW;
    c->txr.env = PVR_TXRENV_MODULATE;
}

static void build_materials(void) {
    pvr_poly_cxt_t base, auxiliary;
    make_context(&base,0,PVR_TXRFMT_RGB565);
    assert(pvr_material_compile_polygon(&environment,&base,0) == 0);
    for(unsigned i = 0; i < MATERIAL_COUNT; ++i) {
        make_context(&base,i == BUMP ? 5 : 1,PVR_TXRFMT_RGB565);
        if(i == BUMP) base.gen.culling = PVR_CULLING_NONE;
        assert(pvr_material_compile_polygon(&surfaces[i],&base,0) == 0);
        if(i == ENVIRONMENT) continue;
        make_context(&auxiliary,i + 1,i == BUMP ? PVR_TXRFMT_BUMP : PVR_TXRFMT_RGB565);
        if(i == BUMP)
            assert(pvr_material_compile_bump(&recipes[i-1],&base,&auxiliary,0) == 0);
        else if(i == LIGHTMAP)
            assert(pvr_material_compile_lightmap(&recipes[i-1],&base,&auxiliary,0,0) == 0);
        else
            assert(pvr_material_compile_emissive(&recipes[i-1],&base,&auxiliary,0,0) == 0);
        assert(recipes[i-1].pass_count == 2);
    }
}

static void sphere_vertex(model_t *m, unsigned u, unsigned v, bool end) {
    shz_sincos_t longitude = shz_sincosf(SHZ_F_TAU * u / SEGMENTS);
    shz_sincos_t latitude = shz_sincosf(SHZ_F_PI * v / RINGS);
    vector_t n = {latitude.sin * longitude.cos,latitude.cos,
                   latitude.sin * longitude.sin,0};
    assert(m->count < VERTICES);
    m->source[m->count] = (pvr_vertex_t){
        .flags=end ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
        .x=n.x,.y=n.y,.z=n.z,.u=(float)u / SEGMENTS,.v=(float)v / RINGS,
        .argb=0xffffffff
    };
    m->normals[m->count++] = n;
}

static void build_models(void) {
    for(unsigned i = 0; i < MATERIAL_COUNT; ++i) {
        model_t *m = &models[i];
        if(i == BUMP) {
            const float xy[6][2] = {{-1.6f,.75f},{1.6f,.75f},{-1.6f,-.75f},
                                    {1.6f,.75f},{1.6f,-.75f},{-1.6f,-.75f}};
            for(unsigned j = 0; j < 6; ++j) {
                m->source[j] = (pvr_vertex_t){
                    .flags=j%3 == 2 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                    .x=xy[j][0],.y=xy[j][1],.z=0,
                    .u=xy[j][0] / 3.2f + .5f,.v=.5f - xy[j][1] / 1.5f,
                    .argb=0xffffffff
                };
                m->normals[j] = (vector_t){0,0,-1,0};
            }
            m->count = 6;
            continue;
        }
        for(unsigned v = 0; v < RINGS; ++v) for(unsigned u = 0; u < SEGMENTS; ++u) {
            sphere_vertex(m,u,v,false); sphere_vertex(m,u+1,v,false);
            sphere_vertex(m,u,v+1,true); sphere_vertex(m,u+1,v,false);
            sphere_vertex(m,u+1,v+1,false); sphere_vertex(m,u,v+1,true);
        }
    }
}

static void prepare_model(unsigned i, float angle, bool enabled) {
    model_t *m = &models[i];
    shz_mat4x4_t r;
    shz_mat4x4_init_rotation_xyz(&r,i == BUMP ? -.35f : -.2f,
                               i == BUMP ? -.28f : angle, .06f);
    if(i == ENVIRONMENT) {
        for(unsigned row = 0; row < 3; ++row) {
            r.elem2D[0][row] *= 1.2f;
            r.elem2D[1][row] *= .82f;
        }
    }
    alignas(32) matrix_t world, projection;
    shz_kos_matrix_export(&world,&r);
    pvr_normal_matrix_t nm;
    assert(pvr_normal_matrix_build(&nm,&world) == 0);
    pvr_normal_stream_t normals = {m->normals,m->count,sizeof(vector_t)};
    assert(pvr_normal_transform(transformed_normals,VERTICES,&normals,&nm,NULL) == 0);
    pvr_light_t light = {.kind=PVR_LIGHT_DIRECTIONAL,
        .source.direction={.6f,.7f,-1,0},.color={1,1,1,0},.intensity=.8f};
    pvr_lighting_context_t lighting = {.ambient={.22f,.22f,.22f},.lights=&light,.light_count=1};
    for(size_t n = 0; n < m->count; ++n) {
        samples[n] = (pvr_lighting_sample_t){.position={0,0,0,1},
            .normal=transformed_normals[n],.color={1,1,1,1}};
    }
    pvr_lighting_stream_t sample_stream = {samples,m->count,sizeof(*samples)};
    assert(pvr_lighting_apply(lit_colors,VERTICES,&sample_stream,&lighting,NULL) == 0);
    for(size_t n = 0; n < m->count; ++n) {
        m->source[n].argb = i == BUMP || (i == ENVIRONMENT && enabled) ?
                            0xffffffff : lit_colors[n];
        if(i == ENVIRONMENT) {
            float uv[2];
            assert(pvr_environment_map_uv(uv,&transformed_normals[n]) == 0);
            m->source[n].u = uv[0]; m->source[n].v = uv[1];
        }
    }
    float center_x = i % 2 ? 474 : 166, center_y = i < 2 ? 203 : 371;
    for(unsigned col = 0; col < 4; ++col) {
        float depth = col == 3 ? 4.5f : r.elem2D[col][2];
        projection[col][0] = 244 * r.elem2D[col][0] + center_x * depth;
        projection[col][1] = -244 * r.elem2D[col][1] + center_y * depth;
        projection[col][2] = 0; projection[col][3] = depth;
    }
    pvr_geometry_stream_t input = {m->source,m->count,sizeof(pvr_vertex_t)};
    pvr_geometry_result_t result;
    assert(pvr_geometry_project(m->projected,VERTICES,&input,&projection,&result) == 0);
    assert(result.produced_vertices == m->count);
    projected_count += result.produced_vertices;
    memcpy(m->layer,m->projected,m->count * sizeof(pvr_vertex_t));
    if(i == BUMP && enabled) {
        /* Pack the documented bump-light fields using SH4ZAM directly.
           Elevation is fixed; the azimuth sweeps in this panel's tangent frame. */
        shz_sincos_t elevation = shz_sincosf(.7f);
        uint32_t k2 = (uint32_t)(190 * elevation.sin);
        uint32_t k3 = (uint32_t)(190 * elevation.cos);
        uint32_t azimuth = (uint32_t)(angle * 255 / SHZ_F_TAU);
        uint32_t bump = (65u << 24) | (k2 << 16) | (k3 << 8) | azimuth;
        for(size_t n = 0; n < m->count; ++n) {
            m->projected[n].argb = 0xff000000;
            m->projected[n].oargb = bump;
        }
    }
    else if(i == LIGHTMAP || i == EMISSIVE) {
        for(size_t n = 0; n < m->count; ++n) {
            m->layer[n].argb = i == EMISSIVE ? 0x00ffffff : 0xffffffff;
            m->layer[n].oargb = 0;
        }
    }
    /* Layer steps replay the same projected geometry, without retransforming. */
    for(size_t n = 0; n < m->count; ++n)
        assert(!memcmp(&m->projected[n],&m->layer[n],4 * sizeof(uint32_t)));
}

static void submit(const pvr_material_t *material, const pvr_vertex_t *v, size_t n) {
    assert(pvr_material_submit(material) == 0);
    pvr_geometry_sink_t sink;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    assert(pvr_geometry_sink_emit(&sink,v,n) == 0);
}

int main(void) {
    demo_init(); build_textures(); build_materials(); build_models();
    const char *lines[8] = {
        "MATERIAL GALLERY / 3D LAB", "REAL PVR RECIPES / SH4ZAM GEOMETRY",
        "ENVIRONMENT MAP", "BUMP LIGHTING", "LIGHTMAP MULTIPLY", "EMISSIVE ADD",
        "A ENV B BUMP X LAYERS Y PAUSE START EXIT", NULL
    };
    demo_hud(lines);
    bool enabled[4] = {true,true,true,true}, pause = false;
    float angle = .7f;
    unsigned frame = 0;
#ifndef SHOWCASE_SMOKE
    uint32_t previous = 0;
#endif
    for(;;) {
#ifdef SHOWCASE_SMOKE
        if(frame == 48) break;
        enabled[0] = frame / 12 != 1;
        enabled[1] = frame / 12 != 2;
        enabled[2] = enabled[3] = frame / 12 != 3;
#else
        maple_device_t *dev = maple_enum_type(0,MAPLE_FUNC_CONTROLLER);
        cont_state_t *state = dev ? maple_dev_status(dev) : NULL;
        uint32_t buttons = state ? state->buttons : 0, pressed = buttons & ~previous;
        previous = buttons;
        if(pressed & CONT_START) break;
        if(pressed & CONT_A) enabled[0] = !enabled[0];
        if(pressed & CONT_B) enabled[1] = !enabled[1];
        if(pressed & CONT_X) enabled[2] = enabled[3] = !enabled[2];
        if(pressed & CONT_Y) pause = !pause;
#endif
        if(!pause) angle += .016f;
        if(angle >= SHZ_F_TAU) angle -= SHZ_F_TAU;
        for(unsigned i = 0; i < MATERIAL_COUNT; ++i) prepare_model(i,angle,enabled[i]);
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_prim(&demo_color_header,sizeof(demo_color_header)) == 0);
        for(unsigned i = 0; i < MATERIAL_COUNT; ++i) {
            float x = i % 2 ? 332 : 24, y = i < 2 ? 105 : 273;
            demo_quad(x,y,284,152,.01f,0xff122339,0xff0a1222,0,0,1,1);
            demo_quad(x,y,284,2,.02f,i%2 ? 0xffe9ab79 : 0xff63d9c0,
                      i%2 ? 0xffe9ab79 : 0xff63d9c0,0,0,1,1);
        }
        for(unsigned i = 0; i < MATERIAL_COUNT; ++i) {
            const pvr_material_t *material = !enabled[i] ? &surfaces[i] :
                i == ENVIRONMENT ? &environment : &recipes[i-1].passes[0].material;
            submit(material,models[i].projected,models[i].count);
        }
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        for(unsigned i = 1; i < MATERIAL_COUNT; ++i) if(enabled[i]) {
            submit(&recipes[i-1].passes[1].material,models[i].layer,models[i].count);
            ++completed_layers;
        }
        assert(pvr_prim(&demo_font_header,sizeof(demo_font_header)) == 0);
        demo_text(0,26,22,1,0xffe9f4ff);
        demo_text(1,27,56,.7f,0xff84a2bd);
        for(unsigned i = 0; i < 4; ++i)
            demo_text(i+2,i%2 ? 344 : 36,i<2 ? 115 : 283,.65f,
                      enabled[i] ? 0xffd3eee8 : 0xff677588);
        demo_text(6,26,446,.72f,0xff9eb6cc);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        ++frame;
    }
    assert(pvr_wait_render_done() == 0);
    for(unsigned i = 0; i < 6; ++i) pvr_mem_free(textures[i]);
    demo_finish();
#ifdef SHOWCASE_SMOKE
    assert(projected_count && completed_layers);
    printf("RESULT: PASS (material gallery; %u frames, %zu projected vertices, %zu layer completions)\n",
           frame,projected_count,completed_layers);
#else
    printf("Material gallery closed after %u frames\n",frame);
#endif
    return 0;
}
