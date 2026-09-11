/* KallistiOS ##version##

   Opaque/translucent compound materials, with depth occlusion.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/pvr_chunk_binding.h>
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static uint16_t pixels[64 * 64];
static pvr_material_recipe_t recipes[4];

static void upload_constant(pvr_txr_surface_t *surface, uint16_t value) {
    for(size_t i = 0; i < 64 * 64; ++i)
        pixels[i] = value;
    for(uint32_t i = 0; i < surface->mip_levels; ++i) {
        pvr_txr_level_info_t level;
        assert(pvr_txr_surface_get_level(surface, i, &level) == 0);
        /* Every texel is identical, so linear and twiddled byte sequences
           coincide. Level placement still uses checked layout metadata. */
        assert(pvr_txr_surface_upload_level(surface, i, pixels, level.byte_size,
                                           PVR_TXR_TRANSFER_CPU) == 0);
    }
}

static void rectangle(float x, float y, float w, float h, float z,
                       uint32_t argb, uint32_t oargb) {
    alignas(32) pvr_vertex_t v[4] = {
        { .flags = PVR_CMD_VERTEX, .x = x, .y = y + h, .z = z,
          .u = 0, .v = 1, .argb = argb, .oargb = oargb },
        { .flags = PVR_CMD_VERTEX, .x = x, .y = y, .z = z,
          .u = 0, .v = 0, .argb = argb, .oargb = oargb },
        { .flags = PVR_CMD_VERTEX, .x = x + w, .y = y + h, .z = z,
          .u = 1, .v = 1, .argb = argb, .oargb = oargb },
        { .flags = PVR_CMD_VERTEX_EOL, .x = x + w, .y = y, .z = z,
          .u = 1, .v = 0, .argb = argb, .oargb = oargb }
    };
    pvr_geometry_vertex_sink_t sink;
    assert(pvr_geometry_vertex_sink_init_current(
        &sink, PVR_GEOMETRY_VERTEX_CANONICAL) == 0);
    assert(pvr_geometry_vertex_sink_emit(&sink, v, 4) == 0);
}

static void draw_step(size_t object, size_t step) {
    const pvr_material_recipe_pass_t *pass = &recipes[object].passes[step];
    bool bump = pass->role == PVR_MATERIAL_PASS_BUMP;
    uint32_t color = bump ? UINT32_C(0xff000000) : UINT32_MAX;
    if(pass->role == PVR_MATERIAL_PASS_EMISSIVE)
        color = UINT32_C(0x00ffffff);
    /* Zero-angle bump texels with overhead lighting produce K1 = 128/255.
       The RGB result should be half-bright, without losing surface alpha. */
    uint32_t offset = bump ? pvr_pack_bump(.5f, F_PI / 2, 0) : 0;
    assert(pvr_material_submit(&pass->material) == 0);
    rectangle(object % 2 ? 352 : 40, object / 2 ? 272 : 64,
              248, 144, .5f, color, offset);
}

#if RECIPE_VERIFY_PIXELS
static unsigned check_pixel(const vid_framebuffer_info_t *fb, unsigned x, unsigned y,
                         unsigned red, unsigned green, unsigned blue) {
    const volatile uint16_t *address = (const volatile uint16_t *)
        ((const uint8_t *)fb->address + y * fb->stride_bytes + x * 2);
    uint16_t pixel = *address;
    unsigned r = ((pixel >> 11) & 31) * 255 / 31;
    unsigned g = ((pixel >> 5) & 63) * 255 / 63;
    unsigned b = (pixel & 31) * 255 / 31;
    printf("pixel %u,%u: %u,%u,%u (expected %u,%u,%u)\n",
           x, y, r, g, b, red, green, blue);
    return abs((int)r - (int)red) > 8 || abs((int)g - (int)green) > 8 ||
           abs((int)b - (int)blue) > 8;
}

static void verify_framebuffer(void) {
    vid_framebuffer_info_t fb;
    assert(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED, &fb) == 0);
    assert(fb.pixel_mode == PM_RGB565 && fb.width == 640 && fb.height == 480);
    /* Independent reference: C=(.8,.4,.2), A=8/15, h=128/255,
       background=(.1,.2,.3); transparent output is C*A + bg*(1-A). */
    unsigned failures = check_pixel(&fb, 20, 20, 26, 51, 77);
#if RECIPE_LAYERS
    /* L=(6/15,5/15,4/15): top is C*L; bottom is clamp(C+L).
       Source alpha is applied only once after composition on the right. */
    failures += check_pixel(&fb, 100, 120, 82, 34, 14);
    failures += check_pixel(&fb, 420, 120, 55, 42, 43);
    failures += check_pixel(&fb, 100, 328, 255, 187, 119);
    failures += check_pixel(&fb, 420, 328, 148, 124, 99);
#else
    failures += check_pixel(&fb, 100, 120, 204, 102, 51);
    failures += check_pixel(&fb, 420, 120, 121, 78, 63);
    failures += check_pixel(&fb, 100, 328, 102, 51, 26);
    failures += check_pixel(&fb, 420, 328, 67, 51, 49);
#endif
    failures += check_pixel(&fb, 164, 120, 32, 32, 32);
    failures += check_pixel(&fb, 164, 328, 32, 32, 32);
    printf("framebuffer comparison: %u mismatches\n", failures);
    assert(failures == 0);
    puts("RESULT: PASS (framebuffer recipe colors and depth occlusion)");
}
#endif

int main(void) {
    const pvr_init_params_t params = {
        .opb_sizes = { PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0 },
        .vertex_buf_size = 512 * 1024,
        .autosort_disabled = !RECIPE_AUTOSORT_DIAGNOSTIC,
        .opb_overflow_count = 1
    };
    pvr_txr_surface_t color, auxiliary;
    pvr_chunk_texture_binding_t entries[2];
    pvr_chunk_texture_table_t table = { entries, 2 };
    pvr_chunk_texture_table_view_t textures;
    pvr_material_t occluder;
    pvr_poly_cxt_t context;
    pvr_pipeline_status_t pipeline;

    vid_set_mode(DM_640x480, PM_RGB565);
    assert(pvr_init(&params) == 0);
    puts(RECIPE_AUTOSORT_DIAGNOSTIC ?
         "material recipes: AUTOSORT DIAGNOSTIC (not ordering conformance)" :
         "material recipes: PVR initialized (presort)");
    pvr_set_bg_color(.1f, .2f, .3f);
    assert(pvr_txr_surface_alloc(&color, 64, 64, PVR_TXR_SURFACE_ARGB4444,
                                 PVR_TXR_SURFACE_TWIDDLED, true) == 0);
    assert(pvr_txr_surface_alloc(&auxiliary, 64, 64,
                                 RECIPE_LAYERS ? PVR_TXR_SURFACE_ARGB4444 :
                                                 PVR_TXR_SURFACE_BUMP,
                                 PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    upload_constant(&color, UINT16_C(0x8c63));
    /* Alpha 1/15 deliberately differs from both zero and one: layered
       shading must ignore it rather than attenuating the surface alpha. */
    upload_constant(&auxiliary, RECIPE_LAYERS ? UINT16_C(0x1654) : 0);
    entries[0] = (pvr_chunk_texture_binding_t){ 7, 0, &color };
    entries[1] = (pvr_chunk_texture_binding_t){ 19, 0, &auxiliary };
    assert(pvr_chunk_texture_table_open(&table, &textures) == 0);
    puts("material recipes: procedural texture levels uploaded");
    puts(RECIPE_LAYERS ? "recipe profiles: lightmap/emissive" :
                         "recipe profiles: trilinear/bump");
    for(size_t i = 0; i < 4; ++i) {
        pvr_poly_cxt_t layer;
        pvr_chunk_material_context_t resolved_surface, resolved_layer;
        pvr_material_recipe_t reference;
        pvr_chunk_render_state_t state = { 0 };
        pvr_chunk_strip_view_t strip = { 0 };
        pvr_list_t list = i % 2 ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
        pvr_poly_cxt_txr(&context, list, PVR_TXRFMT_ARGB4444, 64, 64,
                         color.vram, PVR_FILTER_BILINEAR);
        context.txr.mipmap = true;
        context.txr.mipmap_bias = PVR_MIPBIAS_1_75;
        context.txr.env = PVR_TXRENV_MODULATEALPHA;
        context.gen.culling = PVR_CULLING_NONE;
        state.present = PVR_CHUNK_RENDER_TEXTURE;
        state.texture.identifier = 7;
        state.texture.filter = PVR_FILTER_BILINEAR;
        state.texture.mipmap_adjust = PVR_MIPBIAS_1_75;
        state.strip_flags = context.gen.alpha ? PVR_CHUNK_STRIP_USE_ALPHA : 0;
        strip.type = PVR_CHUNK_STRIP_UV8_FIXED;
        strip.flags = state.strip_flags;
        assert(pvr_chunk_material_resolve_context(
            &resolved_surface, &context, &textures, &state, &strip) == 0);
        assert(resolved_surface.context.txr.base == color.vram);
        assert(resolved_surface.compile_flags == 0);
#if RECIPE_LAYERS
        int (*compile)(pvr_material_recipe_t *, const pvr_poly_cxt_t *,
                       const pvr_poly_cxt_t *, uint32_t, uint32_t) =
            i < 2 ? pvr_material_compile_lightmap : pvr_material_compile_emissive;
        layer = context;
        layer.txr.base = auxiliary.vram;
        layer.txr.mipmap = false;
        layer.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
        state.texture.identifier = 19;
        assert(pvr_chunk_material_resolve_context(
            &resolved_layer, &context, &textures, &state, &strip) == 0);
        assert(resolved_layer.context.txr.base == auxiliary.vram);
        assert(compile(&reference, &context, &layer, 0, 0) == 0);
        assert(compile(&recipes[i], &resolved_surface.context,
                       &resolved_layer.context, resolved_surface.compile_flags,
                       resolved_layer.compile_flags) == 0);
#else
        if(i < 2) {
            assert(pvr_material_compile_trilinear(&reference, &context, 0) == 0);
            assert(pvr_material_compile_trilinear(
                &recipes[i], &resolved_surface.context,
                resolved_surface.compile_flags) == 0);
        }
        else {
            layer = context;
            layer.txr.base = auxiliary.vram;
            layer.txr.format = PVR_TXRFMT_BUMP;
            layer.txr.mipmap = false;
            /* Bias is inactive without mipmaps. Match the resource resolver's
               canonical normal value instead of inheriting the color bias. */
            layer.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
            assert(pvr_material_compile_bump(&reference, &context, &layer, 0) == 0);
            state.texture.identifier = 19;
            assert(pvr_chunk_material_resolve_context(
                &resolved_layer, &context, &textures, &state, &strip) == 0);
            assert(resolved_layer.context.txr.base == auxiliary.vram);
            /* The recipe has a shared sampling policy, not independently
               supersampled inputs. Never silently merge mismatched flags. */
            assert(resolved_surface.compile_flags == resolved_layer.compile_flags);
            assert(pvr_material_compile_bump(
                &recipes[i], &resolved_surface.context, &resolved_layer.context,
                resolved_surface.compile_flags) == 0);
        }
#endif
        /* Compare actual TA packets, not struct padding or test doubles. The
           explicit context reference bypasses Compact resource resolution. */
        assert(recipes[i].pass_count == reference.pass_count);
        assert(recipes[i].requires_presort == reference.requires_presort);
        for(size_t j = 0; j < reference.pass_count; ++j) {
            assert(recipes[i].passes[j].role == reference.passes[j].role);
            assert(recipes[i].passes[j].material.list ==
                   reference.passes[j].material.list);
            if(memcmp(&recipes[i].passes[j].material.header,
                      &reference.passes[j].material.header,
                      sizeof(pvr_poly_hdr_t))) {
                uint32_t actual[8], expected[8];
                memcpy(actual, &recipes[i].passes[j].material.header,
                       sizeof(actual));
                memcpy(expected, &reference.passes[j].material.header,
                       sizeof(expected));
                for(size_t k = 0; k < 8; ++k)
                    printf("recipe %u step %u word %u: %08lx expected %08lx\n",
                           (unsigned)i, (unsigned)j, (unsigned)k,
                           (unsigned long)actual[k], (unsigned long)expected[k]);
            }
            assert(!memcmp(&recipes[i].passes[j].material.header,
                           &reference.passes[j].material.header,
                           sizeof(pvr_poly_hdr_t)));
        }
        assert(recipes[i].requires_presort);
    }
    puts("RESULT: PASS (asset-resolved recipes match explicit TA packets)");
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    assert(pvr_material_compile_polygon(&occluder, &context, 0) == 0);
    puts("material recipes: four profiles compiled");
    for(unsigned frame = 0; frame < 120; ++frame) {
        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        draw_step(0, 0);
        draw_step(2, 0);
        assert(pvr_material_submit(&occluder) == 0);
        rectangle(156, 64, 16, 144, 1, 0xff202020, 0);
        rectangle(156, 272, 16, 144, 1, 0xff202020, 0);
        assert(pvr_list_finish() == 0);
        assert(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
        draw_step(0, 1);
        draw_step(2, 1);
        for(size_t object = 1; object < 4; object += 2)
            for(size_t step = 0; step < recipes[object].pass_count; ++step)
                draw_step(object, step);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        if(frame == 0)
            puts("material recipes: first scene submitted");
    }
    assert(pvr_wait_render_done() == 0);
    assert(pvr_get_pipeline_status(&pipeline) == 0);
    assert(pipeline.faults.mask == PVR_FAULT_NONE);
#if RECIPE_VERIFY_PIXELS
    thd_sleep(40);
    verify_framebuffer();
#endif
    puts("RESULT: PASS (recipe submission; visual comparison still required)");
    thd_sleep(30000);
    pvr_txr_surface_release(&auxiliary);
    pvr_txr_surface_release(&color);
    assert(pvr_shutdown() == 0);
    return 0;
}
