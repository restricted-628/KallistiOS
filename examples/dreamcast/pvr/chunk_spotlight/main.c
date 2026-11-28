/* Move a prepared cone across a host-cooked Compact room. */
#include <kos.h>
#include <dc/pvr_chunk_cache_asset.h>
#include <dc/pvr/pvr_txr.h>
#include <sh4der_kos_spotlight.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "room-section.inc"

enum { WIDTH = 64, HEIGHT = 64, MAX_STRIP = 26 };
static alignas(32) unsigned char storage[64*1024];
static alignas(32) pvr_vertex_t workspace[MAX_STRIP];
static uint16_t pixels[WIDTH*HEIGHT], first[WIDTH*HEIGHT];
static pvr_chunk_cache_draw_t room;
static pvr_poly_hdr_t header;

#define CHECK(expr) do { if(!(expr)) { \
    printf("COMPACT SPOTLIGHT FAIL line %d: %s (errno %d)\n", __LINE__, #expr, errno); \
    fflush(stdout); return -1; } } while(0)

/* w = 1 + z/4. Keep the screen center fixed as depth changes. */
static const matrix_t projection = {{8,0,0,0},{0,8,0,0},{8,8,0,.25f},{32,32,1,1}};
static const matrix_t display = {{52,0,0,0},{0,52,0,0},{80,60,0,.25f},{320,240,1,1}};
static const matrix_t rotated_projection = {{8,8,0,.25f},{0,8,0,0},{-8,0,0,0},{8,-24,1,0}};

static int rotate_room(uint16_t index, pvr_deform_vertex_t *v, void *data) {
    (void)index;
    (void)data;
    point_t p = v->position;
    vector_t n = v->normal;
    v->position.x = p.z + 4;
    v->position.y = p.y + 3;
    v->position.z = 1 - p.x;
    v->normal.x = n.z;
    v->normal.z = -n.x;
    return 0;
}

static int begin_wall_strip(const pvr_chunk_cached_strip_t *strip, void *data) {
    (void)strip;
    (void)data;
    return pvr_prim(&header, sizeof(header));
}

static int draw(const sh4der_kos_spotlight_t *light, const matrix_t *matrix,
                pvr_chunk_cache_resolve_vertex_t resolve, pvr_txr_surface_t *target) {
    sh4der_kos_spotlight_plan_t plan;
    pvr_geometry_sink_t sink;
    CHECK(sh4der_kos_spotlight_prepare(&plan, light) == 0);
    CHECK(pvr_wait_ready() == 0);
    if(target) {
        for(unsigned i = 0; i < WIDTH*HEIGHT; ++i)
            pixels[i] = 0xf81f;
        CHECK(pvr_txr_surface_upload(target, pixels, sizeof(pixels), PVR_TXR_TRANSFER_CPU) == 0);
        CHECK(pvr_txr_surface_begin_render(target, WIDTH, HEIGHT) == 0);
    }
    else
        pvr_scene_begin();
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
    CHECK(pvr_geometry_sink_init_current(&sink) == 0);
    CHECK(pvr_chunk_model_cache_draw_emit(&room, matrix, &sink, workspace, MAX_STRIP,
        NULL, begin_wall_strip, resolve, sh4der_kos_spotlight_vertex, &plan, NULL) == 0);
    CHECK(pvr_list_finish() == 0);
    pvr_render_ticket_t ticket;
    CHECK(pvr_scene_finish_tracked(&ticket) == 0);
    if(target) {
        CHECK(pvr_render_ticket_wait(&ticket, PVR_RENDER_STAGE_COMPLETE, 2000) == 0);
        CHECK(pvr_txr_surface_readback(target, pixels, sizeof(pixels)) == 0);
    }
    return 0;
}

static unsigned red_at(unsigned x) {
    return pixels[32*WIDTH+x] >> 11;
}

int main(void) {
    CHECK(pvr_chunk_cache_section_load_ordinary(room_section, sizeof(room_section),
                                                storage, sizeof(storage), &room) == 0);
    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_init_params_t init = {.opb_sizes={PVR_BINSIZE_16}, .vertex_buf_size=256*1024};
    CHECK(pvr_init(&init) == 0);
    pvr_set_bg_color(0, 0, 1);
    pvr_poly_cxt_t context;
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.shading = PVR_SHADE_GOURAUD;
    pvr_poly_compile(&header, &context);

    pvr_txr_surface_t target = {0};
    CHECK(pvr_txr_surface_alloc(&target, WIDTH, HEIGHT, PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR, false) == 0);
    sh4der_kos_spotlight_t spot = {
        .position={.x=-1.5f,.y=0,.z=-2}, .axis={.x=0,.y=0,.z=1},
        .cutoff_cos=.8f, .radius=4, .constant=1, .rgb=0xff8040
    };
    CHECK(draw(&spot, &projection, NULL, &target) == 0);
    printf("SPOT left RGB565 %04x falloff %04x opposite %04x\n",
           pixels[32*WIDTH+20], pixels[32*WIDTH+27], pixels[32*WIDTH+44]);
    unsigned green = (pixels[32*WIDTH+20] >> 5) & 63u;
    unsigned blue = pixels[32*WIDTH+20] & 31u;
    CHECK(green >= 26 && green <= 34 && blue >= 5 && blue <= 10);
    CHECK(pixels[0] == 0x001f && red_at(20) >= 27 && red_at(27) > 3 &&
          red_at(27) + 3 < red_at(20) && pixels[32*WIDTH+44] == 0);
    memcpy(first, pixels, sizeof(first));
    spot.position.x = 1.5f;
    CHECK(draw(&spot, &projection, NULL, &target) == 0);
    CHECK(red_at(44) >= 27 && pixels[32*WIDTH+20] == 0 && pixels[0] == 0x001f);

    /* Rotate/translate both room and light, undo that transform in projection. */
    sh4der_kos_spotlight_t rotated = spot;
    rotated.position = shz_vec3_init(2,3,2.5f); /* transformed left-hand pose */
    rotated.axis = shz_vec3_init(1,0,0);
    CHECK(draw(&rotated, &rotated_projection, rotate_room, &target) == 0);
    CHECK(memcmp(first, pixels, sizeof(first)) == 0);

    spot.position.x = 0;
    spot.axis = shz_vec3_init(.6f,0,.8f);
    spot.cutoff_cos = .9f;
    CHECK(draw(&spot, &projection, NULL, &target) == 0);
    CHECK(red_at(44) >= 20 && pixels[32*WIDTH+32] == 0);
    spot.radius = 1.75f;
    CHECK(draw(&spot, &projection, NULL, &target) == 0);
    CHECK(pixels[32*WIDTH+44] == 0 && pixels[0] == 0x001f);
    spot.position.z = 2;
    spot.axis = shz_vec3_init(0,0,-1);
    spot.radius = 4;
    CHECK(draw(&spot, &projection, NULL, &target) == 0);
    CHECK(pixels[32*WIDTH+32] == 0 && pixels[0] == 0x001f);
    pvr_txr_surface_release(&target);

    spot.position.z = -2;
    spot.axis = shz_vec3_init(0,0,1);
    spot.cutoff_cos = .8f;
    CHECK(draw(&spot, &display, NULL, NULL) == 0);
    CHECK(pvr_wait_render_done() == 0);
    puts("COMPACT SPOTLIGHT ROOM PASS");
    fflush(stdout);
    for(;;) {
        unsigned tick = timer_ms_gettime64() % 4000;
        float phase = (float)(tick < 2000 ? tick : 4000 - tick) / 2000.0f;
        spot.position.x = -1.5f + 3.0f * phase;
        CHECK(draw(&spot, &display, NULL, NULL) == 0);
    }
}
