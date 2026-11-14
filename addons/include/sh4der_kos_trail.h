/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_TRAIL_H
#define KOS_SH4DER_TRAIL_H
#include <sh4der_kos_image.h>
#include <sh4der_kos_overlay.h>
#include <sh4der_kos_frame.h>
#include <sh4der/sh4der_trail.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One borrowed history slot. Bind each ping-pong image once, publish its exact
   producer after successful scene finish. No allocation, submission or waits.
   Caller owns all producer AND reader retirement before overwrite/release.
   A cut invalidates selection, not GPU uses. Fields immutable except via API. */
typedef struct sh4der_kos_trail_history {
    const sh4der_kos_image_t *image;
    pvr_ptr_t address;
    unsigned width,height,stride;
    sh4der_kos_frame_id_t id;
    pvr_render_id_t last_render;
} sh4der_kos_trail_history_t;
/* Host/load or resource replacement. RGB565 linear/stride history, NEAREST
   image bound with edge UVs for 1:1 sampling. width/height match rendered region.
   New nonzero generation on resize/replacement/PVR restart; reset public camera
   state too. Descriptor/image/storage remain stable. Failure preserves out. */
int sh4der_kos_trail_bind(sh4der_kos_trail_history_t *out,
    const sh4der_kos_image_t *image, unsigned width,unsigned height,uint64_t generation);
/* Call BEFORE any overwrite, failed capture, cut or history discard. Does not
   retire GPU reads, reset camera state, or change generation/monotonic counter. */
void sh4der_kos_trail_invalidate(sh4der_kos_trail_history_t *history);
/* Latest submitted producer only; validates exact target/extent/generation and
   monotonic ID once. Pending GPU work is allowed under ordinary KOS scene
   ordering; this publishes metadata, not CPU-read readiness. Failure preserves
   history, so invalidate before attempting a write that might fail. */
int sh4der_kos_trail_publish(sh4der_kos_trail_history_t *history,
    const pvr_render_ticket_t *producer,uint64_t generation);
/* Complete TR packets: keep(current*(1-weight)), then1..3 shifted ADD copies.
   Explicit expected history ID must match; target must be a distinct retained
   RTT address (never sample the texture being rendered). All checks precede
   output. Cut/zero emits0 without requiring valid history. rect is in actual
   output pixels and must match history dimensions (no scaling). Shifts are in
   those same pixels. Exposed edges retain dimmed current color, pinned policy.
   z>0, dz>0/distinct; reserve increasing depths. max output4. Borrow history
   through the consuming render's COMPLETE; producer COMPLETE alone is not this.
   f is from public trail_step with finite bounded camera/focal inputs; custom
   frames admitted: weight/copy_w[0,1], copies1..3, weights agree within1e-5.
   Integer offsets bounded to +/-history dimensions. No raw scanout/parity mask,
   global stride/dither/scale change, or per-frame header compilation. */
int sh4der_kos_trail_packets(const sh4der_kos_trail_history_t *history,
    sh4der_kos_frame_id_t expected,pvr_ptr_t target,const sh4der_kos_overlay_t *overlay,
    const sh4der_trail_frame_t *f,const float rect[4],float z,float dz,
    sh4der_kos_screen_packet_t *out,size_t capacity);
/* Same retained route, one history copy using public TAA copy_px. Apply public
   jitter to the current scene projection before rendering; keep composited
   output as next history. Reset camera and invalidate slots on cut/resize. */
int sh4der_kos_taa_packets(const sh4der_kos_trail_history_t *history,
    sh4der_kos_frame_id_t expected,pvr_ptr_t target,const sh4der_kos_overlay_t *overlay,
    const sh4der_taa_frame_t *f,const float rect[4],float z,float dz,
    sh4der_kos_screen_packet_t *out,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
