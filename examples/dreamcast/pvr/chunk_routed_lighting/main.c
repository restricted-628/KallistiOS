/* Two independently lit material volumes on one cached Compact receiver. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos.h>
#include <dc/pvr_material.h>
#include <dc/pvr_chunk_cache_asset.h>
#include <dc/pvr/pvr_txr.h>
#include <dc/pvr/pvr_misc.h>

#include <sh4der_kos_routed_lighting.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

enum { TARGET_WIDTH = 64, TARGET_HEIGHT = 64, RECEIVER_VERTICES = 4 };

#define CHECK(call) do { \
    if(!(call)) { \
        printf("ROUTED LIGHTING FAIL at line %d: %s (errno=%d)\n", \
               __LINE__, #call, errno); \
        fflush(stdout); \
        return EXIT_FAILURE; \
    } \
} while(0)

#include "receiver-section.inc"

static alignas(32) unsigned char cache_storage[2048];
static alignas(32) pvr_chunk_two_volume_vertex_t workspace[RECEIVER_VERTICES];
static uint16_t pixels[TARGET_WIDTH * TARGET_HEIGHT];
static uint16_t compact_image[TARGET_WIDTH * TARGET_HEIGHT];

static const matrix_t object_to_screen = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}
};

/* Cover the right side of the receiver. Inverse-Z 2 is closer than the
   receiver at 1. Sample points avoid the edges and diagonal seams. */
static const pvr_modifier_vol_t volume[] = {
    {.flags=PVR_CMD_VERTEX_EOL,
     .ax=32, .ay=57, .az=2, .bx=32, .by=7, .bz=2, .cx=57, .cy=57, .cz=2},
    {.flags=PVR_CMD_VERTEX_EOL,
     .ax=32, .ay=7, .az=2, .bx=57, .by=57, .bz=2, .cx=57, .cy=7, .cz=2},
};

typedef struct render_context {
    const pvr_material_t *material;
    sh4der_kos_routed_lighting_plan_t *lights;
} render_context_t;

static int begin_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    const render_context_t *context = data;
    (void)strip;
    return pvr_material_submit(context->material);
}

static int light_vertex(const pvr_chunk_render_state_t *state, uint16_t index,
                        const pvr_deform_vertex_t *deformation,
                        pvr_geometry_vertex_format_t format,
                        pvr_chunk_two_volume_vertex_t *vertex, void *data) {
    const render_context_t *context = data;
    return sh4der_kos_routed_lighting_vertex(state, index, deformation, format, vertex,
                                  context->lights);
}

static int draw_case(const pvr_chunk_two_volume_cache_draw_t *draw,
                     const pvr_material_t *material,
                     const pvr_mod_hdr_t modifier_headers[2],
                     const pvr_txr_surface_t *target, sh4der_kos_routed_lighting_plan_t *lights,
                     const pvr_vertex_tpcm_t *direct) {
    pvr_geometry_vertex_sink_t sink;
    pvr_render_ticket_t ticket;
    pvr_chunk_cache_result_t result;
    render_context_t context = {material, lights};

    CHECK(pvr_wait_ready() == 0);
    /* A skipped draw cannot inherit a passing readback from its predecessor. */
    for(unsigned i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i)
        pixels[i] = 0xf81f;
    CHECK(pvr_txr_surface_upload(target, pixels, sizeof(pixels), PVR_TXR_TRANSFER_CPU) == 0);
    CHECK(pvr_txr_surface_begin_render(target, TARGET_WIDTH, TARGET_HEIGHT) == 0);
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
    if(direct) {
        CHECK(pvr_material_submit(material) == 0);
        CHECK(pvr_prim(direct, RECEIVER_VERTICES * sizeof(*direct)) == 0);
    }
    else {
        CHECK(pvr_geometry_vertex_sink_init_current(
                  &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED) == 0);
        CHECK(pvr_chunk_model_two_volume_cache_draw_emit(
                  draw, &object_to_screen, &sink, workspace, RECEIVER_VERTICES,
                  NULL, begin_strip, NULL, light_vertex, &context, &result) == 0);
        CHECK(result.emitted_strips == 1 && result.emitted_vertices == RECEIVER_VERTICES);
    }
    CHECK(pvr_list_finish() == 0);
    CHECK(pvr_list_begin(PVR_LIST_OP_MOD) == 0);
    for(unsigned i = 0; i < 2; ++i) {
        CHECK(pvr_prim(&modifier_headers[i], sizeof(modifier_headers[i])) == 0);
        CHECK(pvr_prim(&volume[i], sizeof(volume[i])) == 0);
    }
    CHECK(pvr_list_finish() == 0);
    CHECK(pvr_scene_finish_tracked(&ticket) == 0);
    CHECK(pvr_render_ticket_wait(&ticket, PVR_RENDER_STAGE_COMPLETE, 1000) == 0);
    CHECK(pvr_txr_surface_readback(target, pixels, sizeof(pixels)) == 0);
    return 0;
}

/* Allow two RGB565 steps per channel after texturing and offset addition. */
static bool color_matches(uint16_t pixel, uint16_t expected) {
    return abs((pixel >> 11) - (expected >> 11)) <= 2 &&
           abs(((pixel >> 5) & 63) - ((expected >> 5) & 63)) <= 2 &&
           abs((pixel & 31) - (expected & 31)) <= 2;
}

/* Check every interior pixel even after an error so both material routes
   and the independent direct submission have a recorded result. */
static bool check_image(const char *path, unsigned id, uint16_t outside, uint16_t inside) {
    unsigned misses = 0;
    for(unsigned y = 16; y < 48; ++y) {
        for(unsigned x = 12; x < 52; ++x) {
            const uint16_t expected = x < 32 ? outside : inside;
            const uint16_t actual = pixels[y * TARGET_WIDTH + x];
            if(expected == 0 ? actual != 0 : !color_matches(actual, expected)) {
                if(!misses)
                    printf("RTT mismatch path=%s case=%u x=%u y=%u actual=%04x expected=%04x\n",
                           path, id, x, y, actual, expected);
                ++misses;
            }
        }
    }
    printf("ROUTED %s case=%u outside=%04x inside=%04x background=%04x misses=%u\n",
           path, id, pixels[23 * TARGET_WIDTH + 20], pixels[24 * TARGET_WIDTH + 44],
           pixels[0], misses);
    return misses == 0 && pixels[0] == 0x001f;
}

int main(void) {
    pvr_chunk_cache_section_view_t section;
    pvr_chunk_two_volume_cache_t cache;
    pvr_chunk_two_volume_cache_draw_t draw;
    CHECK(pvr_chunk_cache_section_open(receiver_section, sizeof(receiver_section), &section) == 0);
    CHECK(pvr_chunk_cache_section_materialize_two_volume(
              &section, cache_storage, sizeof(cache_storage), &cache) == 0);
    CHECK(pvr_chunk_model_two_volume_cache_draw_prepare(&cache, &draw) == 0);

    pvr_init_params_t params = {
        .opb_sizes={PVR_BINSIZE_16, PVR_BINSIZE_16, 0, 0, 0},
        .vertex_buf_size=256 * 1024
    };
    vid_set_mode(DM_640x480, PM_RGB565);
    CHECK(pvr_init(&params) == 0);
    pvr_set_bg_color(0, 0, 1);
    pvr_set_shadow_scale(false, 1.0f);
    pvr_txr_surface_t target = {0}, texture = {0};
    CHECK(pvr_txr_surface_alloc(&target, TARGET_WIDTH, TARGET_HEIGHT,
              PVR_TXR_SURFACE_RGB565, PVR_TXR_SURFACE_LINEAR, false) == 0);
    CHECK(pvr_txr_surface_alloc(&texture, 8, 8,
              PVR_TXR_SURFACE_RGB565, PVR_TXR_SURFACE_LINEAR, false) == 0);
    uint16_t white[64];
    for(unsigned i = 0; i < 64; ++i)
        white[i] = 0xffff;
    CHECK(pvr_txr_surface_upload(&texture, white, sizeof(white), PVR_TXR_TRANSFER_CPU) == 0);
    pvr_ptr_t texture_address;
    CHECK(pvr_txr_surface_get_texture_address(&texture, &texture_address) == 0);

    pvr_poly_cxt_t context;
    const uint32_t texture_format = pvr_txr_surface_pvr_format(&texture);
    pvr_poly_cxt_txr_mod(&context, PVR_LIST_OP_POLY,
        texture_format, 8, 8, texture_address, PVR_FILTER_NONE,
        texture_format, 8, 8, texture_address, PVR_FILTER_NONE);
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.specular = true;
    context.txr.env = PVR_TXRENV_MODULATE;
    context.txr2.env = PVR_TXRENV_MODULATE;
    pvr_material_t material;
    CHECK(pvr_material_compile_two_volume(&material, &context, 0) == 0);
    pvr_mod_hdr_t modifier_headers[2];
    pvr_mod_compile(&modifier_headers[0], PVR_LIST_OP_MOD, PVR_MODIFIER_OTHER_POLY,
                    PVR_CULLING_NONE);
    pvr_mod_compile(&modifier_headers[1], PVR_LIST_OP_MOD, PVR_MODIFIER_INCLUDE_LAST_POLY,
                    PVR_CULLING_NONE);

    const sh4der_kos_routed_directional_light_t lights[] = {
        {shz_vec3_init(0, 0, 1), {1, 0, 0}, 0.5f, false},
        {shz_vec3_init(0.8660254f, 0, 0.5f), {0, 1, 0}, 0.5f, false},
        {shz_vec3_init(0, 0.6f, -0.8f), {0, 0, 1}, 0.625f, true}
    };
    const struct {
        sh4der_kos_routed_masks_t masks;
        unsigned active_lights;
        uint32_t base0, offset0, base1, offset1;
        uint16_t outside, inside;
    } cases[] = {
        {{{0, 0}, {0, 0}}, 0, 0, 0, 0, 0, 0x0000, 0x0000},
        {{{1, 0}, {0, 1}}, 1, 0x800000, 0, 0, 0x800000, 0x8000, 0x8000},
        {{{1, 2}, {0, 1}}, 2, 0x800000, 0x004000, 0, 0x800000, 0x8200, 0x8000},
        {{{1, 2}, {4, 1}}, 3, 0x800000, 0x004000, 0x000080, 0x800000, 0x8200, 0x8010}
    };
    sh4der_kos_routed_lighting_plan_t plans[sizeof(cases) / sizeof(cases[0])];
    alignas(32) pvr_vertex_tpcm_t packets[RECEIVER_VERTICES];
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pvr_geometry_vertex_sink_t sink;
        CHECK(sh4der_kos_routed_lighting_prepare(&plans[i], lights, 3, cases[i].masks, cache.format, NULL) == 0);
        CHECK(plans[i].count == cases[i].active_lights);
        CHECK(pvr_geometry_vertex_sink_init_memory(
                  &sink, PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED,
                  packets, RECEIVER_VERTICES) == 0);
        CHECK(pvr_chunk_model_two_volume_cache_draw_emit(
                  &draw, &object_to_screen, &sink, workspace, RECEIVER_VERTICES,
                  NULL, NULL, NULL, sh4der_kos_routed_lighting_vertex, &plans[i], NULL) == 0);
        CHECK(sink.emitted_vertices == RECEIVER_VERTICES);
        for(unsigned j = 0; j < RECEIVER_VERTICES; ++j) {
            const float u = (float)(j >> 1);
            const float v = (float)(1u - (j & 1u));
            CHECK(packets[j].x == 8 + 48*u && packets[j].y == 8 + 48*v && packets[j].z == 1);
            CHECK(packets[j].u0 == u && packets[j].v0 == v);
            CHECK(packets[j].u1 == 1.0f - u && packets[j].v1 == v);
            CHECK(packets[j].flags == (j + 1 == RECEIVER_VERTICES ?
                                       PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
            CHECK(packets[j].argb0 == (0xff000000u | cases[i].base0));
            CHECK(packets[j].argb1 == (0xff000000u | cases[i].base1));
            CHECK(packets[j].oargb0 == cases[i].offset0);
            CHECK(packets[j].oargb1 == cases[i].offset1);
        }
    }
    puts("COMPACT ROUTED LIGHTING NATIVE PACKETS PASS");
    fflush(stdout);

    bool images_ok = true;
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(draw_case(&draw, &material, modifier_headers, &target, &plans[i], NULL) == 0);
        if(!check_image("compact", i, cases[i].outside, cases[i].inside))
            images_ok = false;
    }

    memcpy(compact_image, pixels, sizeof(pixels));

    /* Hand-authored golden for the three-light case, independently of the
       Compact cache, SH4ZAM callback and geometry sink. */
    alignas(32) pvr_vertex_tpcm_t direct[RECEIVER_VERTICES];
    for(unsigned j = 0; j < RECEIVER_VERTICES; ++j) {
        const float u = (float)(j >> 1), v = (float)(1u - (j & 1u));
        direct[j] = (pvr_vertex_tpcm_t){
            .flags=j == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
            .x=8 + 48*u, .y=8 + 48*v, .z=1,
            .u0=u, .v0=v, .argb0=0xff800000, .oargb0=0x00004000,
            .u1=1-u, .v1=v, .argb1=0xff000080, .oargb1=0x00800000
        };
    }
    CHECK(draw_case(NULL, &material, modifier_headers, &target, NULL, direct) == 0);
    if(!check_image("direct", 3, 0x8200, 0x8010))
        images_ok = false;
    if(memcmp(compact_image, pixels, sizeof(pixels)) != 0) {
        puts("Direct and Compact three-light frames differ");
        images_ok = false;
    }
    /* Mirrored U selects the dark half on both sides. Using the other
       volume's U samples white and makes the corresponding base visible. */
    uint16_t stripe[64];
    for(unsigned i = 0; i < 64; ++i)
        stripe[i] = (i % 8 < 4) ? 0 : 0xffff;
    CHECK(pvr_txr_surface_upload(&texture, stripe, sizeof(stripe), PVR_TXR_TRANSFER_CPU) == 0);
    CHECK(draw_case(&draw, &material, modifier_headers, &target, &plans[3], NULL) == 0);
    if(!check_image("mirrored-uv", 3, 0x0200, 0x8000))
        images_ok = false;

    /* The prepared ambient offset remains visible on black texels. */
    const sh4der_kos_routed_masks_t no_lights = {{0}};
    sh4der_kos_routed_ambient_t ambient = {.outside={0, 0x004000}};
    sh4der_kos_routed_lighting_plan_t ambient_plan;
    CHECK(sh4der_kos_routed_lighting_prepare(&ambient_plan, NULL, 0, no_lights,
                                  cache.format, &ambient) == 0);
    CHECK(draw_case(&draw, &material, modifier_headers, &target, &ambient_plan, NULL) == 0);
    if(!check_image("inherited-ambient", 0, 0x0200, 0x0200))
        images_ok = false;
    ambient.separate_inside = true;
    CHECK(sh4der_kos_routed_lighting_prepare(&ambient_plan, NULL, 0, no_lights,
                                  cache.format, &ambient) == 0);
    CHECK(draw_case(&draw, &material, modifier_headers, &target, &ambient_plan, NULL) == 0);
    if(!check_image("black-ambient-override", 0, 0x0200, 0))
        images_ok = false;
    pvr_txr_surface_release(&texture);
    pvr_txr_surface_release(&target);
    pvr_shutdown();
    CHECK(images_ok);
    puts("COMPACT ROUTED LIGHTING RTT PASS");
    fflush(stdout);
    for(;;)
        thd_sleep(1000);
}
