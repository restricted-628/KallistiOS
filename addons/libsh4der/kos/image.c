/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_image.h>
#include <errno.h>
#include <math.h>

int sh4der_kos_image_bind(sh4der_kos_image_t *out,
    const pvr_txr_surface_t *s, unsigned w, unsigned h,
    pvr_filter_mode_t filter, bool centers, bool clamp) {
    if(!out || ((uintptr_t)out & 31u) || !s || !w || !h ||
       w > s->width || h > s->height || s->mipmapped ||
       (s->format != PVR_TXR_SURFACE_RGB565 &&
        s->format != PVR_TXR_SURFACE_ARGB1555 && s->format != PVR_TXR_SURFACE_ARGB4444) ||
       (s->layout != PVR_TXR_SURFACE_LINEAR && s->layout != PVR_TXR_SURFACE_TWIDDLED &&
        s->layout != PVR_TXR_SURFACE_STRIDE) ||
       (filter != PVR_FILTER_NEAREST && filter != PVR_FILTER_BILINEAR)) {
        errno = EINVAL; return -1;
    }
    pvr_ptr_t address;
    if(pvr_txr_surface_get_texture_address(s, &address) < 0) return -1;
    uint32_t format = pvr_txr_surface_pvr_format(s);
    if(format == UINT32_MAX) return -1;
    if((uintptr_t)address & 31u) { errno = EINVAL; return -1; }
    unsigned tw = 8;
    while(tw < s->width) tw <<= 1; /* admitted native width <=1024 */
    sh4der_kos_image_t next = {.surface = s};
    float iw = 1.0f / tw, ih = 1.0f / s->height;
    next.uv[0] = centers ? .5f * iw : 0;
    next.uv[1] = centers ? .5f * ih : 0;
    next.uv[2] = (w - (centers ? 1u : 0u)) * iw;
    next.uv[3] = (h - (centers ? 1u : 0u)) * ih;
    static const pvr_blend_mode_t src[4] = {
        PVR_BLEND_ONE, PVR_BLEND_SRCALPHA, PVR_BLEND_ONE, PVR_BLEND_ZERO
    }, dst[4] = {
        PVR_BLEND_ZERO, PVR_BLEND_INVSRCALPHA, PVR_BLEND_ONE, PVR_BLEND_DESTCOLOR
    };
    for(unsigned i = 0; i < 4; ++i) {
        pvr_poly_cxt_t c;
        pvr_poly_cxt_txr(&c, i ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY,
                        format, tw, s->height, address, filter);
        c.gen.culling = PVR_CULLING_NONE;
        c.gen.fog_type = PVR_FOG_DISABLE;
        c.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        c.depth.write = PVR_DEPTHWRITE_DISABLE;
        c.txr.env = i == SH4DER_KOS_IMAGE_ALPHA ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_MODULATE;
        c.txr.uv_clamp = clamp ? PVR_UVCLAMP_UV : PVR_UVCLAMP_NONE;
        c.blend.src = src[i]; c.blend.dst = dst[i];
        pvr_poly_compile(&next.headers[i], &c);
    }
    *out = next;
    return 0;
}

int sh4der_kos_image_upload(const sh4der_kos_image_t *image,
    const void *pixels, size_t bytes, const pvr_render_ticket_t *last_use,
    pvr_txr_transfer_t transfer) {
    if(!image || !image->surface || !pixels ||
       (transfer != PVR_TXR_TRANSFER_CPU && transfer != PVR_TXR_TRANSFER_SQ &&
        transfer != PVR_TXR_TRANSFER_DMA)) { errno = EINVAL; return -1; }
    if(bytes != image->surface->byte_size) { errno = EINVAL; return -1; }
    if(last_use) {
        pvr_render_stage_t stage;
        if(pvr_render_ticket_get_stage(last_use, &stage) < 0) return -1;
        if(stage < PVR_RENDER_STAGE_COMPLETE) { errno = EAGAIN; return -1; }
    }
    return pvr_txr_surface_upload(image->surface, pixels, bytes, transfer);
}

int sh4der_kos_image_quad(const sh4der_kos_image_t *image,
    sh4der_kos_image_mode_t mode, const float rect[4], float z, uint32_t color,
    sh4der_kos_screen_packet_t *out) {
    if(!image || !image->surface || !rect || !out || ((uintptr_t)out & 31u) ||
       mode < SH4DER_KOS_IMAGE_COPY || mode > SH4DER_KOS_IMAGE_MULTIPLY) {
        errno = EINVAL; return -1;
    }
    for(unsigned i = 0; i < 4; ++i)
        if(!isfinite(rect[i])) { errno = EDOM; return -1; }
    if(!isfinite(z) || z <= 0 || rect[2] <= 0 || rect[3] <= 0 ||
       !isfinite(rect[0] + rect[2]) || !isfinite(rect[1] + rect[3])) {
        errno = EDOM; return -1;
    }
    out->header = image->headers[mode];
    for(unsigned k = 0; k < 4; ++k) {
        float u = k & 1 ? 1 : 0, v = k & 2 ? 1 : 0;
        out->vertices[k] = (pvr_vertex_t){
            .flags = k == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
            .x = rect[0] + u * rect[2], .y = rect[1] + v * rect[3], .z = z,
            .u = image->uv[0] + u * image->uv[2],
            .v = image->uv[1] + v * image->uv[3], .argb = color
        };
    }
    return 0;
}
