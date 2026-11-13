#include <sh4der_kos_frame.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Hardware boundaries only are mocked. The offset builder, sparse reads,
   channel minima, identity and history implementation are the real adapter. */
static unsigned admissions, queries;
static int admission_error, query_error;
static pvr_render_stage_t stage = PVR_RENDER_STAGE_COMPLETE;
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *s, pvr_ptr_t *p) {
    ++admissions;
    if(admission_error) { errno = admission_error; return -1; }
    *p = s->vram;
    return 0;
}
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *t, pvr_render_stage_t *s) {
    (void)t; ++queries;
    if(query_error) { errno = query_error; return -1; }
    *s = stage;
    return 0;
}

static uint16_t pixels[128 * 64], raw[SH4DER_KOS_FRAME_SAMPLES], dark[SH4DER_KOS_FRAME_SAMPLES];
static uint16_t oldraw[SH4DER_KOS_FRAME_SAMPLES], olddark[SH4DER_KOS_FRAME_SAMPLES];
static void unchanged(sh4der_kos_frame_id_t id) {
    assert(!memcmp(raw, oldraw, sizeof(raw)));
    assert(!memcmp(dark, olddark, sizeof(dark)));
    assert(id.generation == 5 && id.render == 77);
}
static uint16_t minimum(uint16_t a, uint16_t b) {
    unsigned result = 0;
    const unsigned shifts[] = {0, 5, 11}, masks[] = {31, 63, 31};
    for(unsigned c = 0; c < 3; ++c) {
        unsigned ac = (a >> shifts[c]) & masks[c], bc = (b >> shifts[c]) & masks[c];
        result |= (ac < bc ? ac : bc) << shifts[c];
    }
    return result;
}
int main(void) {
    pvr_txr_surface_t surface = {.vram = pixels, .width = 128, .height = 64,
        .format = PVR_TXR_SURFACE_RGB565, .layout = PVR_TXR_SURFACE_LINEAR};
    sh4der_kos_frame_binding_t binding = {0}, saved;
    sh4der_kos_frame_id_t id = {5, 77};
    pvr_render_ticket_t ticket = {.id = 9, .target = pixels, .width = 93,
        .height = 61, .stride = 128, .to_texture = 1};
    for(unsigned y = 0; y < 64; ++y)
        for(unsigned x = 0; x < 128; ++x)
            pixels[y * 128 + x] = x < 93 && y < 61 ? (uint16_t)(x * 311 + y * 997) : 0xdead;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == 0);
    assert(admissions == 1);
    saved = binding;
    assert(sh4der_kos_frame_bind(&binding, &surface, 129, 61, 5) == -1);
    assert(!memcmp(&binding, &saved, sizeof(binding)));
    surface.layout = PVR_TXR_SURFACE_TWIDDLED;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == -1 && errno == ENOTSUP);
    surface.layout = PVR_TXR_SURFACE_STRIDE;
    surface.mipmapped = true;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == -1);
    surface.mipmapped = false;
    surface.format = PVR_TXR_SURFACE_ARGB4444;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == -1);
    surface.format = PVR_TXR_SURFACE_RGB565;
    admission_error = ENOSPC;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == -1 && errno == ENOSPC);
    admission_error = 0;
    surface.vram = (char *)pixels + 1;
    assert(sh4der_kos_frame_bind(&binding, &surface, 93, 61, 5) == -1 && errno == EINVAL);
    surface.vram = pixels;
    assert(!memcmp(&binding, &saved, sizeof(binding)));
    unsigned before = admissions;
    memset(raw, 0x5a, sizeof(raw)); memcpy(oldraw, raw, sizeof(raw));
    memset(dark, 0xa5, sizeof(dark)); memcpy(olddark, dark, sizeof(dark));
    for(stage = PVR_RENDER_STAGE_QUEUED; stage < PVR_RENDER_STAGE_COMPLETE; ++stage) {
        assert(sh4der_kos_frame_read(&binding, &ticket, 5, raw, dark, SH4DER_KOS_FRAME_SAMPLES, &id) == -1);
        assert(errno == EAGAIN); unchanged(id);
    }
    query_error = ENODEV;
    assert(sh4der_kos_frame_read(&binding, &ticket, 5, raw, dark, SH4DER_KOS_FRAME_SAMPLES, &id) == -1);
    assert(errno == ENODEV); unchanged(id); query_error = 0;
    assert(sh4der_kos_frame_read(&binding, &ticket, 5, raw, dark, 4799, &id) == -1);
    assert(errno == ENOSPC); unchanged(id);
    assert(sh4der_kos_frame_read(&binding, &ticket, 6, raw, dark, 4800, &id) == -1);
    unchanged(id);
    pvr_render_ticket_t wrong = ticket;
    wrong.stride = 93;
    assert(sh4der_kos_frame_read(&binding, &wrong, 5, raw, dark, 4800, &id) == -1);
    wrong = ticket; wrong.target = NULL; wrong.to_texture = 0;
    stage = PVR_RENDER_STAGE_DISPLAYED;
    assert(sh4der_kos_frame_read(&binding, &wrong, 5, raw, dark, 4800, &id) == -1);
    unchanged(id); stage = PVR_RENDER_STAGE_COMPLETE;
    assert(sh4der_kos_frame_read(&binding, &ticket, 5, raw, dark, 4800, &id) == 0);
    assert(id.generation == 5 && id.render == 9);
    for(unsigned y = 0; y < 60; ++y)
        for(unsigned x = 0; x < 80; ++x) {
            unsigned px = 93 * (2 * x + 1) / 160, py = 61 * (2 * y + 1) / 120;
            unsigned qx = px + 3 < 93 ? px + 3 : 92, qy = py + 3 < 61 ? py + 3 : 60;
            assert(raw[y * 80 + x] == pixels[py * 128 + px]);
            assert(dark[y * 80 + x] == minimum(pixels[py * 128 + px], pixels[qy * 128 + qx]));
        }
    assert(sh4der_kos_frame_read(&binding, &ticket, 5, raw, NULL, 4800, &id) == 0);
    assert(admissions == before); /* No descriptor admission in the read loop. */
    assert(sh4der_kos_frame_bind(&binding, &surface, 1, 1, 6) == 0);
    ticket.width = ticket.height = 1;
    assert(sh4der_kos_frame_read(&binding, &ticket, 6, raw, dark, 4800, &id) == 0);
    for(unsigned i = 0; i < 4800; ++i) assert(raw[i] == pixels[0] && dark[i] == pixels[0]);

    sh4der_kos_frame_history_t history, prior;
    sh4der_kos_frame_metadata_t meta = {{5, 1}, 2, 0.693147f, 0.25f}, got, old;
    assert(sh4der_kos_frame_history_reset(&history, 5) == 0);
    assert(sh4der_kos_frame_history_put(&history, &meta) == 0);
    assert(sh4der_kos_frame_history_get(&history, meta.id, &got) == 0);
    assert(got.gain == 2 && got.glow == 0.25f); old = got;
    meta.id.render = 9; meta.gain = 3;
    assert(sh4der_kos_frame_history_put(&history, &meta) == 0);
    assert(sh4der_kos_frame_history_get(&history, old.id, &got) == -1 && errno == ENOENT);
    assert(!memcmp(&got, &old, sizeof(got)));
    assert(sh4der_kos_frame_history_get(&history, meta.id, &got) == 0 && got.gain == 3);
    prior = history; meta.gain = NAN;
    assert(sh4der_kos_frame_history_put(&history, &meta) == -1 && errno == EDOM);
    assert(!memcmp(&history, &prior, sizeof(history)));
    assert(sh4der_kos_frame_history_reset(&history, 6) == 0);
    assert(sh4der_kos_frame_history_get(&history, meta.id, &got) == -1 && errno == ENOENT);
    meta.gain = 1;
    assert(sh4der_kos_frame_history_put(&history, &meta) == -1 && errno == EINVAL);
    meta.id.generation = 6;
    assert(sh4der_kos_frame_history_put(&history, &meta) == 0);
    assert(sh4der_kos_frame_history_get(&history, meta.id, &got) == 0);
    assert(queries == 7);
    puts("PASS: retained grid, padded pitch, minima, admission/failure preservation, producer states, generation and history eviction");
    return 0;
}
