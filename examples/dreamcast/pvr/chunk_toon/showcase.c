/* KallistiOS ##version##

   Interactive Compact-model surface shading showcase.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#ifdef NDEBUG
#error "The surface showcase requires assertions: rendering calls use assert()."
#endif
#include <dc/sh4zam.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define SEGMENTS 24u
#define RINGS 12u
#define MAX_VERTICES (SEGMENTS * RINGS)
#define MAX_TRIANGLES (2u * MAX_VERTICES)
#define INDEX_ENTRIES (((MAX_VERTICES + PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE - 1u) / \
                        PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE) * PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE)
#define HUD_WIDTH 512u
#define HUD_HEIGHT 256u
#define SMOKE_FRAMES 480u
#ifndef SHOWCASE_START_STYLE
#define SHOWCASE_START_STYLE 0
#endif

enum { STYLE_TOON, STYLE_DIFFUSE, STYLE_SPECULAR, STYLE_WIRE, STYLE_OUTLINE,
       STYLE_COUNT };

typedef struct mesh {
    uint32_t vertices[3u + 6u * MAX_VERTICES];
    uint16_t polygons[8u + 4u * MAX_TRIANGLES];
    pvr_chunk_vertex_index_entry_t index[INDEX_ENTRIES];
    pvr_chunk_model_t model;
    pvr_chunk_model_cache_t cache;
    pvr_chunk_cache_draw_t draw;
    void *storage;
    unsigned vertex_count, triangle_count;
} mesh_t;

typedef struct settings {
    unsigned shape, bands, palette, equation, outline, style;
    bool spin, moving_light;
} settings_t;

static mesh_t meshes[2];
static alignas(32) uint16_t hud_pixels[HUD_WIDTH * HUD_HEIGHT];
static pvr_ptr_t hud_texture;
static pvr_ptr_t white_texture;
static size_t highlight_vertices;
static pvr_poly_hdr_t surface_header, outline_header, panel_header, hud_header;
static pvr_poly_hdr_t lit_header, wire_header;
static const char *const style_names[] = {
    "TOON", "DIFFUSE", "SPECULAR", "WIREFRAME", "OUTLINED"
};
static const char *const reference_names[] = {
    "REFERENCE / 2 BANDS", "UNLIT COLOR", "DIFFUSE ONLY", "ALL EDGES", "DIFFUSE ONLY"
};
static const char *const live_names[] = {
    "", "SMOOTH DIFFUSE", "DIFFUSE + SPECULAR", "SHADED + EDGES", "DIFFUSE + OUTLINE"
};
static const char *const equations[] = { "DOT", "INVERTED", "HALF-LAMBERT" };
static const uint32_t palettes[3][5] = {
    { 0xff172d46, 0xff245c78, 0xff329da7, 0xff79d6c0, 0xffe5ffe5 },
    { 0xff351c45, 0xff73385a, 0xffbb5a63, 0xffed9975, 0xffffdfa1 },
    { 0xff1c2435, 0xff48566c, 0xff8191a4, 0xffbbc8d5, 0xfff2f6ff }
};

static void add_vertex(mesh_t *mesh, float x, float y, float z,
                       float nx, float ny, float nz) {
    const float values[] = { x, y, z, nx, ny, nz };
    assert(mesh->vertex_count < MAX_VERTICES);
    memcpy(mesh->vertices + 2u + 6u * mesh->vertex_count++, values, sizeof(values));
}

static void add_triangle(mesh_t *mesh, unsigned a, unsigned b, unsigned c) {
    assert(mesh->triangle_count < MAX_TRIANGLES);
    assert(a < mesh->vertex_count && b < mesh->vertex_count && c < mesh->vertex_count);
    uint16_t *out = mesh->polygons + 7u + 4u * mesh->triangle_count++;
    out[0] = 3;
    out[1] = a;
    out[2] = b;
    out[3] = c;
}

/* Closed meshes, outward winding, smooth normals; generated only at startup. */
static void build_mesh(mesh_t *mesh, bool sphere) {
    if(sphere) {
        add_vertex(mesh, 0, 1, 0, 0, 1, 0);
        for(unsigned v = 1; v < RINGS; ++v) {
            shz_sincos_t latitude = shz_sincosf(SHZ_F_PI * v / RINGS);
            for(unsigned u = 0; u < SEGMENTS; ++u) {
                shz_sincos_t longitude = shz_sincosf(SHZ_F_TAU * u / SEGMENTS);
                float x = latitude.sin * longitude.cos;
                float z = latitude.sin * longitude.sin;
                add_vertex(mesh, x, latitude.cos, z, x, latitude.cos, z);
            }
        }
        unsigned bottom = mesh->vertex_count;
        add_vertex(mesh, 0, -1, 0, 0, -1, 0);
        for(unsigned u = 0; u < SEGMENTS; ++u) {
            unsigned next = (u + 1u) % SEGMENTS;
            add_triangle(mesh, 0, 1u + next, 1u + u);
            for(unsigned v = 0; v < RINGS - 2u; ++v) {
                unsigned a = 1u + v * SEGMENTS + u;
                unsigned b = 1u + v * SEGMENTS + next;
                add_triangle(mesh, a, b, a + SEGMENTS);
                add_triangle(mesh, b, b + SEGMENTS, a + SEGMENTS);
            }
            add_triangle(mesh, bottom, bottom - SEGMENTS + u,
                          bottom - SEGMENTS + next);
        }
    }
    else {
        for(unsigned u = 0; u < SEGMENTS; ++u) {
            shz_sincos_t around = shz_sincosf(SHZ_F_TAU * u / SEGMENTS);
            for(unsigned v = 0; v < RINGS; ++v) {
                shz_sincos_t tube = shz_sincosf(SHZ_F_TAU * v / RINGS);
                float radius = 0.72f + 0.30f * tube.cos;
                add_vertex(mesh, radius * around.cos, 0.30f * tube.sin,
                           radius * around.sin, tube.cos * around.cos,
                           tube.sin, tube.cos * around.sin);
            }
        }
        for(unsigned u = 0; u < SEGMENTS; ++u) {
            for(unsigned v = 0; v < RINGS; ++v) {
                unsigned a = u * RINGS + v;
                unsigned b = u * RINGS + (v + 1u) % RINGS;
                unsigned c = ((u + 1u) % SEGMENTS) * RINGS + v;
                unsigned d = ((u + 1u) % SEGMENTS) * RINGS + (v + 1u) % RINGS;
                add_triangle(mesh, a, b, c);
                add_triangle(mesh, b, d, c);
            }
        }
    }
    mesh->vertices[0] = PVR_CHUNK_VERTEX_XYZ_NORMAL |
                       ((1u + 6u * mesh->vertex_count) << 16);
    mesh->vertices[1] = mesh->vertex_count << 16;
    mesh->vertices[2u + 6u * mesh->vertex_count] = 0xff;
    mesh->polygons[0] = PVR_CHUNK_MATERIAL_DIFFUSE;
    mesh->polygons[1] = 2;
    mesh->polygons[2] = mesh->polygons[3] = 0xffff;
    mesh->polygons[4] = PVR_CHUNK_STRIP_INDEX;
    mesh->polygons[5] = 1u + 4u * mesh->triangle_count;
    mesh->polygons[6] = mesh->triangle_count;
    mesh->polygons[7u + 4u * mesh->triangle_count] = 0xff;
    mesh->model = (pvr_chunk_model_t) {
        mesh->vertices, 3u + 6u * mesh->vertex_count,
        mesh->polygons, 8u + 4u * mesh->triangle_count, { 0, 0, 0 }, 1.1f
    };
    pvr_chunk_model_view_t view;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_cache_requirements_t requirements;
    assert(pvr_chunk_model_open(&mesh->model, &view) == 0);
    assert(pvr_chunk_model_plan_build(&view, mesh->index, INDEX_ENTRIES, &plan) == 0);
    assert(pvr_chunk_model_cache_query(&plan, &requirements) == 0);
    mesh->storage = aligned_alloc(32, (requirements.bytes + 31u) & ~(size_t)31u);
    assert(mesh->storage);
    assert(pvr_chunk_model_cache_build(&plan, mesh->storage, requirements.bytes,
                                       NULL, NULL, &mesh->cache) == 0);
    assert(pvr_chunk_model_cache_draw_prepare(&mesh->cache, &mesh->draw) == 0);
}

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    (void)strip;
    return pvr_prim(data, sizeof(pvr_poly_hdr_t));
}

typedef struct lighting_draw {
    pvr_chunk_render_policy_binding_t binding;
    uint32_t color;
} lighting_draw_t;

static int begin_lit_strip(const pvr_chunk_render_state_t *state,
                           const pvr_chunk_strip_view_t *strip, void *data) {
    (void)state;
    (void)strip;
    return pvr_prim(data, sizeof(pvr_poly_hdr_t));
}

static int begin_lit_cached(const pvr_chunk_cached_strip_t *strip, void *data) {
    lighting_draw_t *draw = data;
    return pvr_chunk_render_policy_binding_begin_cached_strip(strip, &draw->binding);
}

static int prepare_lit_vertex(const pvr_chunk_render_state_t *state,
        uint16_t index, const pvr_deform_vertex_t *deformation,
        pvr_vertex_t *vertex, void *data) {
    lighting_draw_t *draw = data;
    vertex->argb = draw->color;
    /* The policy consumes offset RGB as the specular material color. */
    vertex->oargb = draw->binding.policy == PVR_CHUNK_RENDER_POLICY_DIFFUSE_SPECULAR
                   ? 0x00ffffff : 0;
    int result = pvr_chunk_render_policy_binding_prepare_cached_vertex(
        state, index, deformation, vertex, &draw->binding);
    if(result == 0 && (vertex->oargb & 0x00ffffff)) ++highlight_vertices;
    return result;
}

static void quad(float x, float y, float width, float height, float z,
                 uint32_t color, float v0, float v1) {
    const float xy[4][2] = { {x, y}, {x + width, y},
                             {x, y + height}, {x + width, y + height} };
    for(unsigned i = 0; i < 4; ++i) {
        pvr_vertex_t vertex = {
            .flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
            .x = xy[i][0], .y = xy[i][1], .z = z,
            .u = i & 1u ? 1.0f : 0.0f, .v = i & 2u ? v1 : v0,
            .argb = color, .oargb = 0
        };
        assert(pvr_prim(&vertex, sizeof(vertex)) == 0);
    }
}

static void hud_line(unsigned row, const char *text) {
    assert(strlen(text) * BFONT_THIN_WIDTH <= HUD_WIDTH);
    bfont_draw_str_ex(hud_pixels + row * BFONT_HEIGHT * HUD_WIDTH,
                       HUD_WIDTH, 0xffff, 0, 16, false, text);
}

static void update_hud(const settings_t *settings) {
    char line[44];
    memset(hud_pixels, 0, sizeof(hud_pixels));
    snprintf(line, sizeof(line), "%s / SURFACE LAB", style_names[settings->style]);
    hud_line(0, line);
    hud_line(1, "COMPACT GEOMETRY  +  SH4ZAM");
    hud_line(2, reference_names[settings->style]);
    if(settings->style == STYLE_TOON)
        snprintf(line, sizeof(line), "LIVE / %u BANDS", settings->bands);
    else
        snprintf(line, sizeof(line), "%s", live_names[settings->style]);
    hud_line(3, line);
    if(settings->style == STYLE_TOON)
        snprintf(line, sizeof(line), "%s / %s / EDGE %u",
                 settings->shape ? "SPHERE" : "TORUS", equations[settings->equation],
                 settings->outline);
    else
        snprintf(line, sizeof(line), "%s / SHINE %u / EDGE %u",
                 settings->shape ? "SPHERE" : "TORUS", 1u << (settings->bands + 1u),
                 settings->outline);
    hud_line(4, line);
    hud_line(5, "A BANDS/SHINE  B COLOR  X MODEL  Y TOON");
    hud_line(6, "L/R STYLE  UP/DOWN EDGE  LEFT/RIGHT PAUSE");
    snprintf(line, sizeof(line), "SPIN %s   LIGHT %s   START EXIT",
             settings->spin ? "ON" : "OFF", settings->moving_light ? "ON" : "OFF");
    hud_line(7, line);
    /* Never overwrite a texture still in flight; updates only follow input. */
    assert(pvr_wait_render_done() == 0);
    pvr_txr_load(hud_pixels, hud_texture, sizeof(hud_pixels));
}

static void text_row(unsigned row, float x, float y, float scale, uint32_t color) {
    quad(x, y, HUD_WIDTH * scale, BFONT_HEIGHT * scale, 10.0f, color,
         (float)(row * BFONT_HEIGHT) / HUD_HEIGHT,
         (float)((row + 1u) * BFONT_HEIGHT) / HUD_HEIGHT);
}

static size_t draw_model(mesh_t *mesh, const settings_t *settings, unsigned bands,
                         float center, float angle, const vector_t *light,
                         bool live) {
    shz_mat4x4_t rotation;
    alignas(32) matrix_t normal_transform, projection;
    pvr_normal_matrix_t normals;
    pvr_frustum_t frustum;
    shz_mat4x4_init_rotation_xyz(&rotation, -0.55f, angle, 0.12f);
    shz_kos_matrix_export(&normal_transform, &rotation);
    assert(pvr_normal_matrix_build(&normals, &normal_transform) == 0);
    /* Homogeneous projection: view depth is rotated Z + 4. */
    for(unsigned col = 0; col < 4; ++col) {
        float depth = col == 3 ? 4.0f : rotation.elem2D[col][2];
        projection[col][0] = 400.0f * rotation.elem2D[col][0] + center * depth;
        projection[col][1] = -400.0f * rotation.elem2D[col][1] + 237.0f * depth;
        projection[col][2] = 0;
        projection[col][3] = depth;
    }
    assert(pvr_frustum_init(&frustum, &projection, 0, 0, 640, 480, 1, 8) == 0);
    alignas(32) pvr_vertex_t vertices[3], clip[PVR_FRUSTUM_CLIP_MAX_VERTICES];
    alignas(32) pvr_deform_vertex_t deformations[3];
    vector_t normal_scratch[3];
    float shades[3], thresholds[4];
    uint32_t colors[5];
    pvr_toon_triangle_t triangles[9];
    pvr_chunk_toon_workspace_t workspace = {
        vertices, deformations, normal_scratch, shades, 3,
        triangles, 9, clip, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    pvr_chunk_outline_workspace_t outline_workspace = {
        vertices, deformations, 3, clip, PVR_FRUSTUM_CLIP_MAX_VERTICES
    };
    for(unsigned i = 0; i < bands; ++i) {
        colors[i] = palettes[settings->palette][i * 4u / (bands - 1u)];
        if(i + 1u < bands) thresholds[i] = (float)(i + 1u) / bands;
    }
    pvr_chunk_toon_profile_t profile = {
        .equation = settings->equation, .thresholds = thresholds,
        .argb_modulation = colors, .threshold_count = bands - 1u, .epsilon = 1e-5f
    };
    assert(pvr_toon_light_init(&profile.light, light, 1, 0) == 0);
    pvr_geometry_sink_t sink;
    assert(pvr_geometry_sink_init_current(&sink) == 0);
    if(settings->outline && (settings->style == STYLE_TOON ||
                            (settings->style == STYLE_OUTLINE && live))) {
        pvr_chunk_outline_profile_t outline = {
            0.015f * settings->outline, 0xff060b17, 0
        };
        pvr_chunk_outline_result_t result;
        assert(pvr_chunk_model_cache_draw_emit_outline(&mesh->draw, &frustum,
            PVR_CHUNK_CLIP_SPLIT, &outline, &sink, &outline_workspace,
            NULL, begin_strip, NULL, NULL, NULL, &outline_header, &result) == 0);
        assert(result.source_triangles == mesh->triangle_count);
    }
    if(settings->style != STYLE_TOON) {
        size_t emitted = 0;
        if(settings->style != STYLE_WIRE || live) {
            pvr_light_t directional = {
                .kind = PVR_LIGHT_DIRECTIONAL, .source.direction = *light,
                .color = {1, 1, 1, 0}, .intensity = 0.85f
            };
            pvr_lighting_extended_context_t lighting = {
                .ambient = {0.16f, 0.16f, 0.16f},
                .lights = &directional, .light_count = 1,
                .view_position = {0, 0, -4, 1},
                .specular_exponent = (float)(1u << (settings->bands + 1u))
            };
            pvr_chunk_render_policy_t policy = PVR_CHUNK_RENDER_POLICY_DIFFUSE;
            if(settings->style == STYLE_DIFFUSE && !live)
                policy = PVR_CHUNK_RENDER_POLICY_UNLIT;
            if(settings->style == STYLE_SPECULAR && live)
                policy = PVR_CHUNK_RENDER_POLICY_DIFFUSE_SPECULAR;
            pvr_chunk_render_policy_config_t config = {
                .policy = policy, .object_to_world = &normal_transform,
                .lighting = &lighting, .begin_strip = begin_lit_strip,
                .begin_strip_data = &lit_header
            };
            lighting_draw_t draw = {.color = palettes[settings->palette][3]};
            assert(pvr_chunk_render_policy_binding_init(&draw.binding, &config) == 0);
            pvr_chunk_cache_result_t result;
            /* These fixed-size meshes stay wholly inside the view frustum. */
            assert(pvr_chunk_model_cache_draw_emit(&mesh->draw, &projection,
                &sink, vertices, 3, NULL, begin_lit_cached, NULL,
                prepare_lit_vertex, &draw, &result) == 0);
            emitted += result.emitted_vertices;
        }
        if(settings->style == STYLE_WIRE) {
            pvr_chunk_wire_workspace_t wire_workspace = {vertices, deformations, 3};
            pvr_chunk_wire_profile_t wire = {
                .width = 0.6f + 0.3f * settings->outline,
                .argb = live ? 0xff183647 : palettes[settings->palette][3],
                .topology = PVR_CHUNK_WIRE_MESH,
                .color_mode = PVR_CHUNK_WIRE_COLOR_PROFILE
            };
            if(live) {
                /* Scale homogeneous XYZW together: same screen XY, slightly
                   larger reciprocal depth for the overlay, avoiding coplanar
                   depth fighting. This is a demo bias, not edge deduplication. */
                for(unsigned col = 0; col < 4; ++col)
                    for(unsigned row = 0; row < 4; ++row)
                        projection[col][row] *= 0.999f;
                assert(pvr_frustum_init(&frustum, &projection,
                                        0, 0, 640, 480, 1, 8) == 0);
            }
            pvr_chunk_wire_result_t result;
            assert(pvr_chunk_model_cache_draw_emit_wire(&mesh->draw, &frustum,
                PVR_CHUNK_CLIP_SPLIT, &wire, &sink, &wire_workspace,
                NULL, begin_strip, NULL, NULL, NULL, &wire_header, &result) == 0);
            assert(result.source_edges == 3u * mesh->triangle_count);
            emitted += result.emitted_vertices;
        }
        return emitted;
    }
    pvr_chunk_toon_result_t result;
    assert(pvr_chunk_model_cache_draw_emit_toon(&mesh->draw, &normals, &frustum,
        PVR_CHUNK_CLIP_SPLIT, &profile, &sink, &workspace, NULL, begin_strip,
        NULL, NULL, NULL, &surface_header, &result) == 0);
    assert(result.source_triangles == mesh->triangle_count);
    return result.generated_vertices;
}

int main(void) {
    settings_t settings = { 0, 4, 0, PVR_TOON_SHADE_HALF_LAMBERT, 2,
                            SHOWCASE_START_STYLE, true, true };
#if !defined(TOON_SMOKE) && !defined(STYLES_SMOKE)
    uint32_t previous_buttons = 0;
    bool previous_left = false, previous_right = false;
#endif
    float angle = 0.4f, light_angle = 0.7f;
    unsigned frame = 0;
    size_t generated = 0;
    build_mesh(&meshes[0], false);
    build_mesh(&meshes[1], true);
    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.025f, 0.04f, 0.075f);
    pvr_poly_cxt_t context;
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_CCW;
    pvr_poly_compile(&surface_header, &context);
    context.gen.culling = PVR_CULLING_CW;
    pvr_poly_compile(&outline_header, &context);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_compile(&panel_header, &context);
    pvr_poly_compile(&wire_header, &context);
    /* Offset-color specular needs the textured polygon path. White preserves
       the diffuse color while permitting the PVR to add the highlight. */
    alignas(32) uint16_t white_pixels[8 * 8];
    memset(white_pixels, 0xff, sizeof(white_pixels));
    white_texture = pvr_mem_malloc(sizeof(white_pixels));
    assert(white_texture);
    pvr_txr_load(white_pixels, white_texture, sizeof(white_pixels));
    pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, PVR_TXRFMT_RGB565,
                    8, 8, white_texture, PVR_FILTER_NONE);
    context.txr.env = PVR_TXRENV_MODULATE;
    context.gen.culling = PVR_CULLING_CCW;
    context.gen.specular = true;
    pvr_poly_compile(&lit_header, &context);
    hud_texture = pvr_mem_malloc(sizeof(hud_pixels));
    assert(hud_texture);
    pvr_poly_cxt_txr(&context, PVR_LIST_TR_POLY,
        PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
        HUD_WIDTH, HUD_HEIGHT, hud_texture, PVR_FILTER_NONE);
    context.gen.culling = PVR_CULLING_NONE;
    context.depth.comparison = PVR_DEPTHCMP_ALWAYS;
    context.depth.write = false;
    pvr_poly_compile(&hud_header, &context);
    update_hud(&settings);

    for(;;) {
        bool changed = false;
#if defined(STYLES_SMOKE)
        if(frame == STYLE_COUNT * 2u * 12u) break;
        if(frame % 12u == 0) {
            unsigned phase = frame / 12u;
            settings.style = phase % STYLE_COUNT;
            settings.shape = phase / STYLE_COUNT;
            settings.palette = phase % 3u;
            settings.bands = 2u + phase % 4u;
            changed = true;
        }
#elif defined(TOON_SMOKE)
        if(frame == SMOKE_FRAMES) break;
        if(frame % 60u == 0) {
            unsigned phase = frame / 60u;
            settings.bands = 2u + phase % 4u;
            settings.shape = phase / 4u;
            settings.palette = phase % 3u;
            settings.equation = phase % 3u;
            settings.outline = phase % 4u;
            changed = true;
        }
#else
        maple_device_t *controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        cont_state_t *state = controller ? maple_dev_status(controller) : NULL;
        uint32_t buttons = state ? state->buttons : 0;
        uint32_t pressed = buttons & ~previous_buttons;
        previous_buttons = buttons;
        bool left = state && state->ltrig > 128;
        bool right = state && state->rtrig > 128;
        bool style_changed = (left && !previous_left) || (right && !previous_right);
        if(left && !previous_left)
            settings.style = (settings.style + STYLE_COUNT - 1u) % STYLE_COUNT;
        if(right && !previous_right)
            settings.style = (settings.style + 1u) % STYLE_COUNT;
        previous_left = left;
        previous_right = right;
        if(pressed & CONT_START) break;
        if(pressed & CONT_A) settings.bands = 2u + (settings.bands - 1u) % 4u;
        if(pressed & CONT_B) settings.palette = (settings.palette + 1u) % 3u;
        if(pressed & CONT_X) settings.shape ^= 1u;
        if(pressed & CONT_Y) settings.equation = (settings.equation + 1u) % 3u;
        if((pressed & CONT_DPAD_UP) && settings.outline < 5) ++settings.outline;
        if((pressed & CONT_DPAD_DOWN) && settings.outline) --settings.outline;
        if(pressed & CONT_DPAD_LEFT) settings.spin = !settings.spin;
        if(pressed & CONT_DPAD_RIGHT) settings.moving_light = !settings.moving_light;
        changed = pressed != 0 || style_changed;
#endif
        if(changed) update_hud(&settings);
        if(settings.spin) angle += 0.009f;
        if(settings.moving_light) light_angle += 0.006f;
        if(angle >= SHZ_F_TAU) angle -= SHZ_F_TAU;
        if(light_angle >= SHZ_F_TAU) light_angle -= SHZ_F_TAU;
        shz_sincos_t light_rotation = shz_sincosf(light_angle);
        vector_t light = { light_rotation.sin, 0.55f, -light_rotation.cos, 0 };
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        assert(pvr_prim(&panel_header, sizeof(panel_header)) == 0);
        quad(24, 105, 284, 257, 0.01f, 0xff101e31, 0, 1);
        quad(332, 105, 284, 257, 0.01f, 0xff101e31, 0, 1);
        quad(24, 102, 284, 3, 0.02f, 0xff55ddc3, 0, 1);
        quad(332, 102, 284, 3, 0.02f, 0xffffc785, 0, 1);
        generated += draw_model(&meshes[settings.shape], &settings, 2, 166, angle, &light, false);
        generated += draw_model(&meshes[settings.shape], &settings, settings.bands,
                                 474, angle, &light, true);
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        assert(pvr_prim(&hud_header, sizeof(hud_header)) == 0);
        text_row(0, 26, 22, 1, 0xffe9f4ff);
        text_row(1, 27, 54, 0.65f, 0xff84a2bd);
        text_row(2, 36, 115, 0.65f, 0xff83ead0);
        text_row(3, 344, 115, 0.65f, 0xffffd6a4);
        text_row(4, 26, 372, 0.75f, 0xffdce8f5);
        text_row(5, 26, 408, 0.75f, 0xff9eb6cc);
        text_row(6, 26, 428, 0.75f, 0xff9eb6cc);
        text_row(7, 26, 450, 0.65f, 0xff6bcdbd);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        ++frame;
    }
    assert(pvr_wait_render_done() == 0);
    pvr_pipeline_status_t status;
    assert(pvr_get_pipeline_status(&status) == 0 && status.faults.mask == PVR_FAULT_NONE);
#if defined(STYLES_SMOKE)
    assert(generated > 0);
    assert(highlight_vertices > 0);
    printf("RESULT: PASS (styles showcase; %u frames, %zu generated vertices, %zu highlighted vertices)\n",
           frame, generated, highlight_vertices);
#elif defined(TOON_SMOKE)
    assert(generated > 0);
    printf("RESULT: PASS (toon showcase; %u frames, %zu generated vertices)\n", frame, generated);
#else
    printf("Toon showcase: %u frames, %zu generated vertices\n", frame, generated);
#endif
    pvr_mem_free(hud_texture);
    pvr_mem_free(white_texture);
    assert(pvr_shutdown() == 0);
    free(meshes[0].storage);
    free(meshes[1].storage);
    return 0;
}
