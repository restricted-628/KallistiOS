/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black.
   Original bounded RGB565 RTT sampling fixture; no vendor scene backend. */
#include <kos.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(expr) do { if(!(expr)) { printf("FAIL line %d: %s (errno %d)\n", __LINE__, #expr, errno); goto fail; } } while(0)
static uint16_t raw[SH4DER_KOS_FRAME_SAMPLES], dark[SH4DER_KOS_FRAME_SAMPLES];
static sh4der_kos_frame_binding_t binding;
static sh4der_kos_frame_history_t history;

static int panel(const pvr_poly_hdr_t *header, float x0, float x1, uint32_t rgb) {
    pvr_vertex_t v[4] = {
        {.flags=PVR_CMD_VERTEX, .x=x0, .y=0, .z=1, .argb=rgb},
        {.flags=PVR_CMD_VERTEX, .x=x1, .y=0, .z=1, .argb=rgb},
        {.flags=PVR_CMD_VERTEX, .x=x0, .y=64, .z=1, .argb=rgb},
        {.flags=PVR_CMD_VERTEX_EOL, .x=x1, .y=64, .z=1, .argb=rgb}
    };
    if(pvr_prim(header, sizeof(*header)) < 0) return -1;
    return pvr_prim(v, sizeof(v));
}

int main(void) {
    const pvr_init_params_t params = {
        {PVR_BINSIZE_16, 0, 0, 0, 0}, 64 * 1024, 0, 0, 0, 0, 0
    };
    pvr_txr_surface_t surface = {0};
    pvr_render_ticket_t ticket;
    sh4der_kos_frame_id_t id;
    sh4der_kos_frame_metadata_t metadata, result;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    int initialized = 0;
    CHECK(vid_mode->pm == PM_RGB565);
    CHECK(pvr_init(&params) == 0);
    initialized = 1;
    CHECK(pvr_txr_surface_alloc(&surface, 128, 64, PVR_TXR_SURFACE_RGB565,
                               PVR_TXR_SURFACE_LINEAR, false) == 0);
    CHECK(sh4der_kos_frame_bind(&binding, &surface, 96, 64, 1) == 0);
    CHECK(sh4der_kos_frame_history_reset(&history, 1) == 0);
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    context.gen.fog_type = PVR_FOG_DISABLE;
    pvr_poly_compile(&header, &context);
    for(unsigned frame = 0; frame < 10; ++frame) {
        CHECK(pvr_wait_ready() == 0);
        CHECK(pvr_txr_surface_begin_render(&surface, 96, 64) == 0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        CHECK(panel(&header, 0, 48, frame & 1 ? 0xff00ff00 : 0xffff0000) == 0);
        CHECK(panel(&header, 48, 96, 0xff0000ff) == 0);
        CHECK(pvr_list_finish() == 0);
        CHECK(pvr_scene_finish_tracked(&ticket) == 0);
        metadata = (sh4der_kos_frame_metadata_t){{1, ticket.id}, 1, 0, 0};
        CHECK(sh4der_kos_frame_history_put(&history, &metadata) == 0);
        /* A CPU read needs completion. This is not a per-GPU-pass wait. */
        CHECK(pvr_render_ticket_wait(&ticket, PVR_RENDER_STAGE_COMPLETE, 1000) == 0);
        CHECK(sh4der_kos_frame_read(&binding, &ticket, 1, raw, dark,
                                    SH4DER_KOS_FRAME_SAMPLES, &id) == 0);
        CHECK(sh4der_kos_frame_history_get(&history, id, &result) == 0);
        CHECK(result.id.render == ticket.id && result.gain == 1);
        for(unsigned y = 0; y < SH4DER_FRAME_GRID_Y; ++y) {
            CHECK(raw[y * 80 + 10] == (frame & 1 ? 0x07e0 : 0xf800));
            CHECK(raw[y * 80 + 60] == 0x001f);
            CHECK(dark[y * 80 + 38] == 0); /* red/green beside blue */
            CHECK(dark[y * 80 + 60] == 0x001f);
        }
        /* No GPU sampling readers in this fixture; the CPU read just retired.
           The same surface may now receive the next producer. */
    }
    CHECK(sh4der_kos_frame_bind(&binding, &surface, 64, 32, 2) == 0);
    CHECK(sh4der_kos_frame_history_reset(&history, 2) == 0);
    CHECK(sh4der_kos_frame_history_get(&history, id, &result) < 0);
    pvr_txr_surface_release(&surface);
    pvr_shutdown();
    puts("RESULT: PASS (SH4DER retained RGB565 RTT sampling, pitch and identity)");
    return 0;
fail:
    /* Never free a possibly active target on timeout/failure. Shutdown quiesces
       PVR; this bounded process then exits with its app allocation retained. */
    if(initialized) pvr_shutdown();
    return 1;
}
