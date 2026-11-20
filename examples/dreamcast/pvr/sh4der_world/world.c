/* A mirrored scene captured and sampled by Compact without vendor GPU calls.
   Run as an ELF; serial PASS checks both renders, not just the math. */
#include <kos.h>
#include <sh4der_kos_world.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if(!(x)) {                                                                                 \
            printf("WORLD FAIL line %d: %s\n", __LINE__, #x);                                      \
            return 1;                                                                              \
        }                                                                                          \
    } while(0)
enum { W = 64, H = 64 };
static const float receiver[4][3] = {{-3, -3, 0}, {-3, 3, 0}, {3, -3, 0}, {3, 3, 0}};
static const uint16_t polys[] = {PVR_CHUNK_STRIP_INDEX, 6, 1, 4, 0, 1, 2, 3, 0xff};
static uint32_t words[15] = {PVR_CHUNK_VERTEX_XYZ | (13u << 16), 4u << 16};
static pvr_chunk_vertex_index_entry_t indices[256];
static pvr_vertex_t geo[48], projected[48], work[4];
static sh4der_spark_pool_t sparks;
static uint16_t pixels[SH4DER_KOS_FRAME_SAMPLES];
static sh4der_kos_frame_binding_t readback;
typedef struct draw {
    pvr_poly_hdr_t header;
    sh4der_kos_mirror_draw_t layer;
} draw_t;
static int header(const pvr_chunk_render_state_t *s, const pvr_chunk_strip_view_t *v, void *data) {
    (void)s;
    (void)v;
    return pvr_prim(&((draw_t *)data)->header, sizeof(pvr_poly_hdr_t));
}
static int reflection(const pvr_chunk_render_state_t *s, const pvr_chunk_vertex_attributes_t *a,
                      const pvr_chunk_strip_attributes_t *t, pvr_vertex_t *v, void *data) {
    return sh4der_kos_mirror_vertex(s, a, t, v, &((draw_t *)data)->layer);
}

int main(void) {
    const float eye[] = {0, 0, 4}, at[] = {0, 0, 0}, up[] = {0, 1, 0};
    /* The ordinary scene camera looks down -Z. The capture mirrors Z;
       only points exactly in the receiver plane project identically. */
    const matrix_t main_view = {
        {32, 0, 0, 0}, {0, -32, 0, 0}, {-32, -32, 0, -1}, {128, 128, .1f, 4}};
    const sh4der_pl4nar_plane_t plane = {{0, 0, 1}, 0};
    const sh4der_pl4nar_material_t mirror = {
        .f0 = 1, .gain = 1, .ground_keep = 1, .reach = 1, .tint = 0xffffff};
    sh4der_pl4nar_view_t view;
    sh4der_pl4nar_sample_t samples[4];
    matrix_t camera, billboard;
    shz_mat4x4_t before, after;
    shz_xmtrx_init_identity();
    shz_xmtrx_store_4x4(&before);
    sh4der_kos_billboard_y(&billboard, shz_vec3_init(1, 0, 0), shz_vec3_init(2, 0, 4));
    CHECK(billboard[3][0] == 1);
    CHECK(sh4der_kos_mirror_view(&view, &camera, &plane, eye, at, up, 1.5708f, .1f, W, H, 1) == 0);
    const float normals[4][3] = {{0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    CHECK(sh4der_kos_mirror_samples(&view, &mirror, receiver, normals, samples, 4) == 0);
    CHECK(samples[0].down > .6f && samples[1].down == 0);
    CHECK(sh4der_kos_mirror_samples(&view, &mirror, receiver, NULL, samples, 4) == 0);
    CHECK(samples[0].u < .2f && samples[2].u > .8f && samples[0].v > .8f);
    shz_xmtrx_store_4x4(&after);
    CHECK(!memcmp(&before, &after, sizeof(before)));

    /* Prepare transmission and displacement once per indexed model vertex.
       Terrain visibility is supplied in grid order. */
    const float heights[4] = {0, 2, 0, 2}, sun[3] = {1, .25f, 0};
    float shadow[4], scratch[4];
    uint8_t ao[4];
    CHECK(sh4der_kos_heightfield_ao(heights, 2, 2, 1, sun, 0, .1f, shadow, scratch, ao) == 0);
    CHECK(ao[0] == 0 && ao[1] == 255);
    const sh4der_water_wave_t wave = {.dx = 1, .amplitude = .2f, .wavelength = 4, .phase = 1.5708f};
    sh4der_kos_surface_sample_t surface[4] = {0};
    sh4der_kos_water_material_t water = {
        .waves = &wave, .count = 1, .swell = 1, .ripple = 1, .thin_path = .1f, .thick_path = .5f};
    const float tint[3] = {0, 1, 0}, loss[3] = {0, 0, 0}, back[3] = {0, -1, 0};
    sh4der_sss_init(&water.scattering, tint, loss, 1, 0, 0);
    for(unsigned i = 0; i < 4; ++i) {
        float normal[3];
        sh4der_kos_water_sample(&water, receiver[i][0] + 3, 0, 0, 0, up, back, 1, ao[i] / 255.f,
                                &surface[i], normal);
        CHECK(normal[1] > .9f);
    }
    CHECK(surface[0].transmission[1] == 0 && surface[1].transmission[1] > .9f);
    CHECK(surface[0].dy > .19f && surface[2].dy < -.19f);
    sh4der_kos_surface_sample_t solid = {0};
    sh4der_sss_rgb(&water.scattering, up, back, up, .5f, 1, 1, 1, solid.transmission);
    pvr_chunk_vertex_attributes_t attr = {.index = 0};
    pvr_vertex_t solid_vertex = {.argb = 0x80304050, .u = .25f, .y = 3};
    CHECK(sh4der_kos_surface_vertex(NULL, &attr, NULL, &solid_vertex, &solid) == 0);
    CHECK(solid_vertex.argb == 0x8030ff50 && solid_vertex.u == .25f && solid_vertex.y == 3);
    pvr_chunk_model_t model = {words, 15, polys, 9, {0, 0, 0}, 5};
    pvr_chunk_model_view_t model_view;
    pvr_chunk_model_plan_t plan;
    memcpy(words + 2, receiver, sizeof(receiver));
    words[14] = 0xff;
    CHECK(pvr_chunk_model_open(&model, &model_view) == 0);
    CHECK(pvr_chunk_model_plan_build(&model_view, indices, 256, &plan) == 0);
    pvr_geometry_sink_t sink;
    CHECK(pvr_geometry_sink_init_memory(&sink, projected, 4) == 0);
    CHECK(pvr_chunk_model_emit_prepared(&plan, &main_view, &sink, work, 4, NULL,
                                        sh4der_kos_surface_vertex, surface, NULL) == 0);

    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_init_params_t params = {
        {PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0}, 256 * 1024, 0, 0, 0, 0, 0};
    CHECK(pvr_init(&params) == 0);
    pvr_set_bg_color(0, 0, 0);
    pvr_txr_surface_t src = {0}, dst = {0};
    pvr_render_ticket_t producer, reader;
    sh4der_kos_image_t image;
    sh4der_kos_trail_history_t capture;
    CHECK(pvr_txr_surface_alloc(&src, W, H, PVR_TXR_SURFACE_RGB565, PVR_TXR_SURFACE_LINEAR,
                                false) == 0);
    CHECK(pvr_txr_surface_alloc(&dst, W, H, PVR_TXR_SURFACE_RGB565, PVR_TXR_SURFACE_LINEAR,
                                false) == 0);
    CHECK(sh4der_kos_image_bind(&image, &src, W, H, PVR_FILTER_NEAREST, false, true) == 0);
    CHECK(sh4der_kos_trail_bind(&capture, &image, W, H, 1) == 0);
    CHECK(sh4der_kos_frame_bind(&readback, &dst, W, H, 2) == 0);
    pvr_poly_hdr_t pool_header, spark_header;
    pvr_poly_cxt_t ctx;
    pvr_poly_cxt_col(&ctx, PVR_LIST_TR_POLY);
    ctx.gen.culling = PVR_CULLING_NONE;
    ctx.gen.fog_type = PVR_FOG_DISABLE;
    ctx.depth.write = PVR_DEPTHWRITE_DISABLE;
    ctx.blend.src = PVR_BLEND_SRCALPHA;
    ctx.blend.dst = PVR_BLEND_ONE;
    pvr_poly_compile(&pool_header, &ctx);
    /* Two atlas cells: the first is black and the selected second is white. */
    pvr_txr_surface_t atlas = {0};
    static uint16_t texels[8][16] __attribute__((aligned(32)));
    for(unsigned row = 0; row < 8; ++row)
        for(unsigned col = 8; col < 16; ++col)
            texels[row][col] = 0xffff;
    CHECK(pvr_txr_surface_alloc(&atlas, 16, 8, PVR_TXR_SURFACE_RGB565, PVR_TXR_SURFACE_LINEAR,
                                false) == 0);
    CHECK(pvr_txr_surface_upload(&atlas, texels, sizeof(texels), PVR_TXR_TRANSFER_CPU) == 0);
    pvr_ptr_t atlas_address;
    CHECK(pvr_txr_surface_get_texture_address(&atlas, &atlas_address) == 0);
    pvr_poly_cxt_txr(&ctx, PVR_LIST_TR_POLY, pvr_txr_surface_pvr_format(&atlas), 16, 8,
                     atlas_address, PVR_FILTER_BILINEAR);
    ctx.gen.culling = PVR_CULLING_NONE;
    ctx.gen.fog_type = PVR_FOG_DISABLE;
    ctx.depth.write = PVR_DEPTHWRITE_DISABLE;
    ctx.txr.env = PVR_TXRENV_MODULATE;
    ctx.txr.uv_clamp = PVR_UVCLAMP_UV;
    ctx.blend.src = PVR_BLEND_ONE;
    ctx.blend.dst = PVR_BLEND_ONE;
    pvr_poly_compile(&spark_header, &ctx);
    CHECK(pvr_geometry_sink_init_current(&sink) == 0);
    matrix_t pool_to_mirror;
    sh4der_kos_billboard_y(&billboard, shz_vec3_init(0, 0, 1), shz_vec3_init(0, 0, 4));
    shz_kos_matrix_compose_unchecked(&pool_to_mirror, &camera, &billboard);
    const float centre[3] = {0, 0, 0}, u[3] = {2, 0, 0}, v[3] = {0, 2, 0};
    sh4der_pool_vtx_t fan[18];
    unsigned n = sh4der_pool_wedge(centre, u, v, 1, .5f, 4, 0xff0000, 1, fan);
    CHECK(sh4der_kos_pool_vertices(fan, n, false, geo, 48) == 12);
    CHECK(geo[11].flags == PVR_CMD_VERTEX_EOL && geo[11].x == fan[n - 1].x);
    n = sh4der_pool_disc(centre, u, v, 8, 0xff0000, 1, fan);
    CHECK(sh4der_kos_pool_vertices(fan, n, true, geo, 23) == -1);
    int count = sh4der_kos_pool_vertices(fan, n, true, geo, 48);
    CHECK(count == 24);
    pvr_geometry_stream_t stream = {geo, 24, sizeof(*geo)};
    CHECK(pvr_geometry_project(projected, 48, &stream, &pool_to_mirror, NULL) == 0);
    CHECK(pvr_wait_ready() == 0 && pvr_txr_surface_begin_render(&src, W, H) == 0);
    CHECK(pvr_list_begin(PVR_LIST_TR_POLY) == 0 &&
          pvr_prim(&pool_header, sizeof(pool_header)) == 0);
    CHECK(pvr_geometry_sink_emit(&sink, projected, count) == 0);
    const sh4der_spark_view_t sv = {.right = {1, 0, 0},
                                    .up = {0, 1, 0},
                                    .fwd = {0, 0, 1},
                                    .near_z = .1f,
                                    .focal_px = 32,
                                    .cols = 2,
                                    .rows = 1,
                                    .inset = .0625f};
    const sh4der_spark_screen_t screen = {32, 32, 32};
    sh4der_spark_reset(&sparks);
    CHECK(sh4der_kos_spark_vertices(&sparks.s[0], &sv, &screen, work) == 0);
    CHECK(sh4der_spark_billboard(&sparks, (float[3]){.35f, 0, 1}, .08f, 0, 0xff00ff00, 1, 1, 0));
    CHECK(sh4der_kos_spark_vertices(&sparks.s[0], &sv, &screen, work) == 4);
    CHECK(pvr_prim(&spark_header, sizeof(spark_header)) == 0);
    CHECK(pvr_geometry_sink_emit(&sink, work, 4) == 0 && pvr_list_finish() == 0);
    CHECK(pvr_scene_finish_tracked(&producer) == 0);
    CHECK(sh4der_kos_trail_publish(&capture, &producer, 1) == 0);

    draw_t draw = {.layer = {samples, &mirror, &image}};
    CHECK(sh4der_kos_mirror_header(&draw.header, &capture, capture.id, capture.address, false) ==
          -1);
    pvr_ptr_t dest;
    CHECK(pvr_txr_surface_get_texture_address(&dst, &dest) == 0);
    sh4der_kos_frame_id_t stale = capture.id;
    ++stale.generation;
    CHECK(sh4der_kos_mirror_header(&draw.header, &capture, stale, dest, false) == -1);
    CHECK(sh4der_kos_mirror_header(&draw.header, &capture, capture.id, dest, mirror.additive) == 0);
    CHECK(pvr_wait_ready() == 0 && pvr_txr_surface_begin_render(&dst, W, H) == 0);
    CHECK(pvr_list_begin(PVR_LIST_TR_POLY) == 0);
    CHECK(pvr_chunk_model_emit_prepared(&plan, &main_view, &sink, work, 4, header, reflection,
                                        &draw, NULL) == 0);
    CHECK(pvr_list_finish() == 0 && pvr_scene_finish_tracked(&reader) == 0);
    CHECK(pvr_render_ticket_wait(&reader, PVR_RENDER_STAGE_COMPLETE, 1000) == 0);
    sh4der_kos_frame_id_t id;
    CHECK(sh4der_kos_frame_read(&readback, &reader, 2, pixels, NULL, 4800, &id) == 0);
    unsigned red = 0, green = 0;
    for(unsigned i = 0; i < 4800; ++i) {
        red += (pixels[i] >> 11) > 4 && !(pixels[i] & 0x07ff);
        green += (pixels[i] & 0x07e0) > 0x0400;
    }
    printf("WORLD mirror red=%u green=%u center=%04x\n", red, green, pixels[30 * 80 + 40]);
    CHECK(red > 100 && green > 4 && (pixels[30 * 80 + 40] >> 11) >= 25 && pixels[0] == 0);
    sh4der_kos_trail_invalidate(&capture);
    CHECK(sh4der_kos_mirror_header(&draw.header, &capture, capture.id, dest, false) == -1);
    pvr_txr_surface_release(&dst);
    pvr_txr_surface_release(&src);
    pvr_txr_surface_release(&atlas);
    pvr_shutdown();
    puts("WORLD COMPACT RTT PASS");
    return 0;
}
