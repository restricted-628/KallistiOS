/* KallistiOS ##version##

   Fixed-slot texture residency example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <assert.h>
#include <errno.h>
#include <stdalign.h>
#include <stdint.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

#define TEXTURE_SIZE 64u
#define TEXTURE_PIXELS (TEXTURE_SIZE * TEXTURE_SIZE)
#define ASSET_COUNT 3u
#define SLOT_COUNT 2u

static alignas(32) uint16_t encoded[ASSET_COUNT][TEXTURE_PIXELS];
static pvr_txr_residency_slot_t slots[SLOT_COUNT];
static pvr_txr_surface_t surfaces[SLOT_COUNT];

static uint32_t twiddled_index(uint32_t x, uint32_t y) {
    uint32_t x_bits = x;
    uint32_t y_bits = y;

    x_bits = (x_bits | (x_bits << 8)) & UINT32_C(0x00ff00ff);
    x_bits = (x_bits | (x_bits << 4)) & UINT32_C(0x0f0f0f0f);
    x_bits = (x_bits | (x_bits << 2)) & UINT32_C(0x33333333);
    x_bits = (x_bits | (x_bits << 1)) & UINT32_C(0x55555555);
    y_bits = (y_bits | (y_bits << 8)) & UINT32_C(0x00ff00ff);
    y_bits = (y_bits | (y_bits << 4)) & UINT32_C(0x0f0f0f0f);
    y_bits = (y_bits | (y_bits << 2)) & UINT32_C(0x33333333);
    y_bits = (y_bits | (y_bits << 1)) & UINT32_C(0x55555555);
    return y_bits | (x_bits << 1);
}

static void build_assets(void) {
    uint32_t asset;
    uint32_t x;
    uint32_t y;

    for(asset = 0; asset < ASSET_COUNT; ++asset) {
        for(y = 0; y < TEXTURE_SIZE; ++y) {
            for(x = 0; x < TEXTURE_SIZE; ++x) {
                uint16_t red = (uint16_t)(((x + asset * 9u) & 31u) << 11);
                uint16_t green
                    = (uint16_t)(((y + asset * 17u) & 63u) << 5);
                uint16_t blue
                    = (uint16_t)(((x ^ y ^ (asset * 11u)) & 31u));

                encoded[asset][twiddled_index(x, y)] = red | green | blue;
            }
        }
    }
}

static pvr_txr_surface_t *acquire_asset(
        pvr_txr_residency_t *cache, uint32_t identifier,
        pvr_txr_residency_handle_t *handle) {
    pvr_txr_request_status_t request_status;
    pvr_txr_request_t *request;
    pvr_txr_surface_t *surface;

    if(pvr_txr_residency_acquire(cache, identifier, handle, &surface) == 0)
        return surface;
    assert(errno == ENOENT);

    assert(pvr_txr_residency_reserve(cache, identifier, handle,
                                     &surface) == 0);
    assert(pvr_txr_surface_upload_async(surface, encoded[identifier],
                                        sizeof(encoded[identifier]),
                                        &request) == 0);
    assert(pvr_txr_request_wait(request, 1000, &request_status) == 0);
    assert(request_status.state == PVR_TXR_REQUEST_COMPLETE);
    assert(pvr_txr_request_destroy(request) == 0);
    assert(pvr_txr_residency_publish(cache, *handle) == 0);
    return surface;
}

static void draw_texture(const pvr_poly_hdr_t *header) {
    alignas(32) pvr_vertex_t vertices[4] = {
        { .flags = PVR_CMD_VERTEX, .x = 160.0f, .y = 80.0f, .z = 1.0f,
          .u = 0.0f, .v = 0.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX, .x = 480.0f, .y = 80.0f, .z = 1.0f,
          .u = 1.0f, .v = 0.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX, .x = 160.0f, .y = 400.0f, .z = 1.0f,
          .u = 0.0f, .v = 1.0f, .argb = UINT32_C(0xffffffff) },
        { .flags = PVR_CMD_VERTEX_EOL, .x = 480.0f, .y = 400.0f, .z = 1.0f,
          .u = 1.0f, .v = 1.0f, .argb = UINT32_C(0xffffffff) }
    };

    assert(pvr_prim(header, sizeof(*header)) == 0);
    assert(pvr_prim(vertices, sizeof(vertices)) == 0);
}

int main(int argc, char **argv) {
    static const uint32_t sequence[] = { 0, 1, 0, 2 };
    pvr_txr_residency_t cache;
    pvr_txr_residency_status_t cache_status;
    pvr_txr_surface_t prototype;
    pvr_pipeline_status_t pipeline_status;
    uint32_t frame;

    (void)argc;
    (void)argv;

    assert(pvr_init_defaults() == 0);
    pvr_set_bg_color(0.02f, 0.02f, 0.04f);
    build_assets();

    assert(pvr_txr_surface_init(&prototype, TEXTURE_SIZE, TEXTURE_SIZE,
                                PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_TWIDDLED, false) == 0);
    assert(pvr_txr_residency_init(&cache, slots, surfaces, SLOT_COUNT,
                                  &prototype) == 0);

    for(frame = 0; frame < 120u; ++frame) {
        uint32_t identifier = sequence[frame / 30u];
        pvr_txr_residency_handle_t handle;
        pvr_txr_surface_t *surface = acquire_asset(&cache, identifier,
                                                   &handle);
        pvr_poly_cxt_t context;
        pvr_poly_hdr_t header;
        pvr_ptr_t texture_address;
        uint32_t format = pvr_txr_surface_pvr_format(surface);

        assert(format != UINT32_MAX);
        assert(pvr_txr_surface_get_texture_address(surface,
                                                   &texture_address) == 0);
        pvr_poly_cxt_txr(&context, PVR_LIST_OP_POLY, format,
                         surface->width, surface->height, texture_address,
                         PVR_FILTER_NEAREST);
        pvr_poly_compile(&header, &context);

        assert(pvr_wait_ready() == 0);
        pvr_scene_begin();
        assert(pvr_list_begin(PVR_LIST_OP_POLY) == 0);
        draw_texture(&header);
        assert(pvr_list_finish() == 0);
        assert(pvr_scene_finish() == 0);
        assert(pvr_wait_render_done() == 0);
        assert(pvr_txr_residency_unpin(&cache, handle) == 0);
    }

    assert(pvr_txr_residency_get_status(&cache, &cache_status) == 0);
    assert(cache_status.ready_slots == SLOT_COUNT);
    assert(cache_status.loading_slots == 0 && cache_status.pin_count == 0);
    assert(cache_status.hits == 117 && cache_status.misses == 3);
    assert(cache_status.evictions == 1);
    assert(pvr_get_pipeline_status(&pipeline_status) == 0);
    assert(pipeline_status.faults.mask == PVR_FAULT_NONE);
    assert(pvr_txr_residency_destroy(&cache) == 0);
    assert(pvr_shutdown() == 0);
    return 0;
}
