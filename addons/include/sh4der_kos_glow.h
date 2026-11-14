/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_GLOW_H
#define KOS_SH4DER_GLOW_H
#include <stdalign.h>
#include <sh4der_kos_frame.h>
#include <sh4der_kos_screen.h>
#include <sh4der/sh4der_glow.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Caller RAM. Padding is cleared by prepare; arrays suit CPU/SQ/blocking DMA.
   source names the sampled frame, not a later GPU scene using these textures. */
typedef struct sh4der_kos_glow_stage {
    alignas(32) uint16_t big[SH4DER_GLOW_TEX_W * SH4DER_GLOW_TEX_H];
    alignas(32) uint16_t small[SH4DER_GLOW_SMALL_TEX_W * SH4DER_GLOW_SMALL_TEX_H];
    sh4der_kos_frame_id_t source;
    float mean;
} sh4der_kos_glow_stage_t;

/* Build maps from an 80x60 RGB565 grid. LUT is 256 host/load-prepared weights.
   keep/256 is the share of previous retained (0 ignores previous). When keep is
   nonzero, previous must be from an earlier render in the same generation.
   out == previous is allowed; all other storage ranges must be disjoint.
   Returns 0, or -1/errno without modifying out. No resources or submissions.
   Use a pre-glow source; if sampling a composited frame, remove its gain/glow
   using the exact retained history that produced it before calling prepare.
   Temporal easing is NOT feedback removal. Source pixels are not modified. */
int sh4der_kos_glow_prepare(sh4der_kos_glow_stage_t *out,
    const uint16_t *grid, size_t count, const uint8_t lut[256],
    sh4der_kos_frame_id_t source, const sh4der_kos_glow_stage_t *previous,
    uint8_t keep);

/* Exact sampled-frame association for halo feedback. Borrows immutable CPU
   map staging until correction/discard; this is separate from GPU retirement.
   maps->source identifies the map input, rendered identifies the later scene
   containing that map. Never substitute the newest map for the sampled one. */
typedef struct sh4der_kos_glow_feedback {
    const sh4der_kos_glow_stage_t *maps;
    sh4der_kos_frame_id_t rendered,map_source;
    float big,small,inverse_gain;
} sh4der_kos_glow_feedback_t;
/* Record after successful scene finish, using its exact metadata and all halo
   pass weights summed (each[0,16]). Composition: scene*gain*(1-darken)+halos.
   darken[0,1), combined gain>.00390625 and<=256. Non-gain grades, ray/shaft
   transforms or other screen effects require a pre-effect source instead.
   Failure preserves out. Keep maps immutable; changes to its source invalidate
   this record automatically. Caller retains one record/map per in-flight read. */
int sh4der_kos_glow_feedback_record(sh4der_kos_glow_feedback_t *out,
    const sh4der_kos_frame_metadata_t *rendered,const sh4der_kos_glow_stage_t *maps,
    float big,float small,float darken);
/* Correct80x60 grid in place only after exact ID/map match. Uses pinned nearest
   map estimate against bilinear glow; clipping/quantization cannot be undone.
   Multiple halo passes above1 are supported. Then remove the recorded combined
   gain. Output can feed glow_prepare; for exposure mark corrected metadata gain1.
   Failure leaves grid unchanged. No lookup of current/global state. */
int sh4der_kos_glow_feedback_remove(const sh4der_kos_glow_feedback_t *feedback,
    sh4der_kos_frame_id_t sampled,uint16_t *grid,size_t count);

/* Borrowed surfaces, no allocation or scene ownership. Do not mutate fields.
   Binding and original descriptors/storage remain stable while used. */
typedef struct sh4der_kos_glow_binding {
    const pvr_txr_surface_t *big, *small;
    pvr_poly_hdr_t headers[3]; /* darken, big, small */
    sh4der_kos_frame_id_t source;
    bool ready;
} sh4der_kos_glow_binding_t;

/* Admit distinct, non-overlapping RGB565 LINEAR nonmip surfaces of 128x64 and
   32x16. Compile invariant headers once. Failure leaves out intact; successful
   rebind resets ready. No texture bytes change. Rebind on allocation/descriptor
   replacement or PVR restart. All inputs/output disjoint, out aligned32. */
int sh4der_kos_glow_bind(sh4der_kos_glow_binding_t *out,
    const pvr_txr_surface_t *big, const pvr_txr_surface_t *small);

/* Blocking KOS upload; no scene wait or implicit ownership acquisition.
   last_use must cover ALL submitted producers/readers of both destinations;
   a later ordered scene's ticket may cover earlier scenes. NULL is caller
   assurance of no such outstanding uses (e.g. fresh allocations), not proof.
   A sampled source's producer ticket does NOT retire users of these maps.
   Caller serializes upload against submission, descriptor changes and shutdown.
   Pending last_use returns EAGAIN before changing readiness/VRAM. Once upload
   starts, ready becomes false; ANY transfer failure leaves it false, even if
   the first texture changed. Retry a complete pair before building/submitting
   packets. CPU staging is reusable on return; GPU textures need reader retirement.
   Previously built packets must also not be submitted during mutation/failure. */
int sh4der_kos_glow_upload(sh4der_kos_glow_binding_t *binding,
    const sh4der_kos_glow_stage_t *stage, const pvr_render_ticket_t *last_use,
    pvr_txr_transfer_t transfer);

/* Return 0..3 complete packets: multiply darken, add big, add small; zero-weight
   layers omitted. strength/radius/darken are finite [0,1]. Larger glow requires
   explicit passes, not a silently clamped strength. All checks precede output.
   Caller owns list/sort/depth policy as for grade packets; z_step >0 and enough
   to distinguish emitted layers. Submit whole packets through one KOS sink.
   Retain both texture allocations through every submitted reader; rebuilding
   or clearing the binding does not retire readers. Inputs/output disjoint. */
int sh4der_kos_glow_halo_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z0, float z_step, float strength, float radius,
    float darken, sh4der_kos_screen_packet_t *out, size_t capacity);

#define SH4DER_KOS_GLOW_ROW_VERTICES (2 * (SH4DER_GLOW_MESH_X + 1))
#define SH4DER_KOS_GLOW_MAX_RAYS 6
/* One complete strip per row, same sink for header and vertices. */
typedef struct sh4der_kos_glow_strip {
    pvr_poly_hdr_t header;
    pvr_vertex_t vertices[SH4DER_KOS_GLOW_ROW_VERTICES];
} sh4der_kos_glow_strip_t;

/* Return rays*SH4DER_GLOW_MESH_Y strips, or zero for unlit/offscreen sun.
   rays is 1..6, share [0,1]; sun is normalized screen position (may be outside).
   z0 is the first copy's reciprocal depth, z_step separates copies. Per-vertex
   distance fading needs the subdivided mesh, not only four screen corners.
   capacity counts strips; output aligned32 and disjoint from all inputs.
   Invalid inputs/capacity leave output unchanged. Same map/readiness/lifetime
   and list ordering contracts as halo_packets; no resources or submissions. */
int sh4der_kos_glow_ray_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z0, float z_step, const float sun[2],
    float share, unsigned rays, sh4der_kos_glow_strip_t *out, size_t capacity);

/* One scaled copy: return SH4DER_GLOW_MESH_Y strips, zero if weight is zero.
   anchor, scale and weight are [0,1]; scale<1 pushes the pattern from anchor.
   This is glow's shaft copy, distinct from the separate shafts effect backend. */
int sh4der_kos_glow_shaft_packets(const sh4der_kos_glow_binding_t *binding,
    const float rect[4], float z, const float anchor[2], float scale, float weight,
    sh4der_kos_glow_strip_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
