/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_IMAGE_H
#define KOS_SH4DER_IMAGE_H
#include <sh4der_kos_screen.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum sh4der_kos_image_mode {
    SH4DER_KOS_IMAGE_COPY, SH4DER_KOS_IMAGE_ALPHA,
    SH4DER_KOS_IMAGE_ADD, SH4DER_KOS_IMAGE_MULTIPLY
} sh4der_kos_image_mode_t;

/* Borrowed immutable descriptor/storage. No allocation or content ownership.
   headers[mode]: COPY is OP, all others TR; fog off, ALWAYS/no depth writes.
   uv = {u0,v0,du,dv}, normalized content range. Do not edit fields. */
typedef struct sh4der_kos_image {
    const pvr_txr_surface_t *surface;
    pvr_poly_hdr_t headers[4];
    float uv[4];
} sh4der_kos_image_t;

/* Bind nonmip RGB565/ARGB1555/ARGB4444, LINEAR/TWIDDLED/STRIDE. Content starts
   at (0,0), bounded by storage. centers selects texel centers versus edges;
   clamp=false enables repeating (caller must provide suitable padding/UVs).
   Only NEAREST/BILINEAR filters. All checks/header compilation occur once here.
   Stride header width rounds up to power of two; caller owns shared X32 stride
   and must set it to surface->width for the complete render using this image.
   Failure leaves out unchanged. out aligned32; all storage ranges disjoint.
   Rebind on descriptor/allocation replacement or PVR restart. */
int sh4der_kos_image_bind(sh4der_kos_image_t *out,
    const pvr_txr_surface_t *surface, unsigned content_width,
    unsigned content_height, pvr_filter_mode_t filter, bool centers, bool clamp);

/* Upload complete preformatted bytes through existing blocking CPU/SQ/DMA.
   last_use must cover ALL destination producers/readers; NULL is caller's
   assurance of no outstanding uses. Pending returns EAGAIN before upload.
   CPU source can be reused on return. Any upload error may leave changed VRAM:
   do not submit ANY cached header/packet until a full replacement succeeds.
   This binding does not track readiness, external writers, readers or tickets.
   Caller serializes update/submission/release/shutdown. GPU-only ordered reads
   need ordinary scene ordering, not this CPU wait. */
int sh4der_kos_image_upload(const sh4der_kos_image_t *image,
    const void *pixels, size_t bytes, const pvr_render_ticket_t *last_use,
    pvr_txr_transfer_t transfer);

/* Complete header/quad using cached UVs and packed modulation color. Returns0
   or -1/errno without output changes. rect={x,y,w,h}, z>0 reciprocal depth.
   Caller retains image through all readers and chooses matching list/order.
   Reuse packets for unchanged authored parameters; no submission here. */
int sh4der_kos_image_quad(const sh4der_kos_image_t *image,
    sh4der_kos_image_mode_t mode, const float rect[4], float z, uint32_t color,
    sh4der_kos_screen_packet_t *out);
#ifdef __cplusplus
}
#endif
#endif
