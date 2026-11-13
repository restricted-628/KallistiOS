/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <math.h>
#include <string.h>

int sh4der_kos_frame_bind(sh4der_kos_frame_binding_t *out,
    const pvr_txr_surface_t *surface, uint32_t width, uint32_t height,
    uint64_t generation) {
    sh4der_kos_frame_binding_t next = {0};
    pvr_ptr_t address;

    if(!out || !surface || !generation || !width || !height ||
       width > surface->width || height > surface->height) {
        errno = EINVAL;
        return -1;
    }
    if(surface->format != PVR_TXR_SURFACE_RGB565 || surface->mipmapped ||
       (surface->layout != PVR_TXR_SURFACE_LINEAR &&
        surface->layout != PVR_TXR_SURFACE_STRIDE)) {
        errno = ENOTSUP;
        return -1;
    }
    if(pvr_txr_surface_get_texture_address(surface, &address) < 0)
        return -1;
    if((uintptr_t)address & 1u) {
        errno = EINVAL;
        return -1;
    }
    next.pixels = address;
    next.generation = generation;
    next.width = width;
    next.height = height;
    next.stride = surface->width;
    for(unsigned x = 0; x < SH4DER_FRAME_GRID_X; ++x) {
        size_t px = (uint64_t)width * (2u * x + 1u) /
                    (2u * SH4DER_FRAME_GRID_X);
        next.x[x] = px;
        next.neighbor_x[x] = width - 1u - px < 3u ? width - 1u : px + 3u;
    }
    for(unsigned y = 0; y < SH4DER_FRAME_GRID_Y; ++y) {
        size_t py = (uint64_t)height * (2u * y + 1u) /
                    (2u * SH4DER_FRAME_GRID_Y);
        size_t qy = height - 1u - py < 3u ? height - 1u : py + 3u;
        next.row[y] = py * surface->width;
        next.neighbor_row[y] = qy * surface->width;
    }
    *out = next;
    return 0;
}

int sh4der_kos_frame_read(const sh4der_kos_frame_binding_t *binding,
    const pvr_render_ticket_t *ticket, uint64_t generation,
    uint16_t *raw, uint16_t *dark, size_t capacity, sh4der_kos_frame_id_t *id) {
    pvr_render_stage_t stage;
    if(!binding || !ticket || !raw || !id || !binding->pixels ||
       !generation || generation != binding->generation || !ticket->id ||
       !ticket->to_texture || ticket->target != (const void *)binding->pixels ||
       ticket->width != binding->width || ticket->height != binding->height ||
       ticket->stride != binding->stride) {
        errno = EINVAL;
        return -1;
    }
    if(capacity < SH4DER_KOS_FRAME_SAMPLES) {
        errno = ENOSPC;
        return -1;
    }
    if(pvr_render_ticket_get_stage(ticket, &stage) < 0)
        return -1;
    if(stage < PVR_RENDER_STAGE_COMPLETE) {
        errno = EAGAIN;
        return -1;
    }
    for(unsigned y = 0; y < SH4DER_FRAME_GRID_Y; ++y) {
        const volatile uint16_t *row = binding->pixels + binding->row[y];
        const volatile uint16_t *neighbor = binding->pixels + binding->neighbor_row[y];
        for(unsigned x = 0; x < SH4DER_FRAME_GRID_X; ++x) {
            unsigned i = y * SH4DER_FRAME_GRID_X + x;
            uint16_t pixel = row[binding->x[x]];
            raw[i] = pixel;
            if(dark)
                dark[i] = sh4der_frame_min565(pixel, neighbor[binding->neighbor_x[x]]);
        }
    }
    *id = (sh4der_kos_frame_id_t){generation, ticket->id};
    return 0;
}

int sh4der_kos_frame_history_reset(sh4der_kos_frame_history_t *history,
    uint64_t generation) {
    if(!history || !generation) {
        errno = EINVAL;
        return -1;
    }
    memset(history, 0, sizeof(*history));
    history->generation = generation;
    return 0;
}

int sh4der_kos_frame_history_put(sh4der_kos_frame_history_t *history,
    const sh4der_kos_frame_metadata_t *metadata) {
    if(!history || !metadata || !history->generation ||
       metadata->id.generation != history->generation || !metadata->id.render) {
        errno = EINVAL;
        return -1;
    }
    if(!isfinite(metadata->gain) || metadata->gain <= 0 ||
       !isfinite(metadata->gain_ln) || !isfinite(metadata->glow) || metadata->glow < 0) {
        errno = EDOM;
        return -1;
    }
    history->slots[metadata->id.render % SH4DER_FRAME_HISTORY] = *metadata;
    return 0;
}

int sh4der_kos_frame_history_get(const sh4der_kos_frame_history_t *history,
    sh4der_kos_frame_id_t id, sh4der_kos_frame_metadata_t *out) {
    const sh4der_kos_frame_metadata_t *entry;
    if(!history || !out || !id.generation || !id.render) {
        errno = EINVAL;
        return -1;
    }
    entry = &history->slots[id.render % SH4DER_FRAME_HISTORY];
    if(id.generation != history->generation || entry->id.generation != id.generation ||
       entry->id.render != id.render) {
        errno = ENOENT;
        return -1;
    }
    *out = *entry;
    return 0;
}
