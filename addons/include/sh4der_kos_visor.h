/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_VISOR_H
#define KOS_SH4DER_VISOR_H
#include <sh4der_kos_focus.h>
#include <sh4der_kos_overlay.h>
#include <sh4der/sh4der_visor.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_VISOR_PACKETS (SH4DER_LOOK_LAYERS+2)
typedef struct sh4der_kos_visor_plan {
    sh4der_visor_desc_t description;
    uint16_t lut[SH4DER_VISOR_LUT_BINS];
    alignas(32) uint16_t grain[SH4DER_VISOR_GRAIN_TEX*SH4DER_VISOR_GRAIN_TEX];
    alignas(32) uint16_t scan[SH4DER_VISOR_SCAN_TEX*SH4DER_VISOR_SCAN_TEX];
    pvr_vertex_t vignette[SH4DER_FOCUS_VERTS];
} sh4der_kos_visor_plan_t;
/* Host/load/mode change: OFF/NIGHT/THERMAL/AMBER. Bake LUT, noise/scan and
   virtual640x480 base vignette once. out aligned32; immutable until reprepare. */
int sh4der_kos_visor_prepare(sh4der_kos_visor_plan_t *out, int mode);
/*80x60 grid through prepared LUT into edge-padded128x64 RGB565. Input/output
   disjoint/aligned16; capacity counts texels. No readback or upload here. */
int sh4der_kos_visor_thermal(const sh4der_kos_visor_plan_t *plan,
    const uint16_t *grid, size_t count, uint16_t *out, size_t capacity);
/* Complete ordered TR packets: grade (or thermal ALPHA), optional grain and
   scan MULTIPLY. Images are caller-bound: thermal LINEAR RGB565128x64/content
   80x60 BILINEAR/edge/clamp; grain LINEAR RGB56532x32 BILINEAR/edge/repeat;
   scan LINEAR RGB5658x8 NEAREST/edge/repeat. Patterns can be NULL to omit;
   thermal image required only for active thermal mode. Upload baked patterns
   once/mode change using image_upload with ALL destination readers retired.
   Thermal uses a retained pre-effect frame. No image readiness is inferred.
   amount[0,1] from caller's public visor_fade state; zero emits nothing.
   Grade/thermal fade smoothly; grain/scan depths are baked and are enabled
   for nonzero amount, matching the pinned source's explicit client policy.
   Return0..8; capacity counts packets; failure leaves output unchanged. Caller
   owns sink/order, z>0 and z_step>0 distinct layers, out aligned32/disjoint. */
int sh4der_kos_visor_packets(const sh4der_kos_visor_plan_t *plan,
    const sh4der_kos_overlay_t *overlay, const sh4der_kos_image_t *thermal,
    const sh4der_kos_image_t *grain, const sh4der_kos_image_t *scan,
    float amount, uint32_t frame, const float rect[4], float z, float z_step,
    sh4der_kos_screen_packet_t *out, size_t capacity);
/*408 black-alpha vertices reusing prepared mask (no per-vertex roots). Submit
   overlay ALPHA header and vertices after packets, at caller-selected z.
   Return0 if amount0, else408. out aligned32/disjoint. Bloom remains the
   caller's glow pass at description.bloom*amount over retained pre-effect data. */
int sh4der_kos_visor_vignette(const sh4der_kos_visor_plan_t *plan,
    float amount, const float rect[4], float z, pvr_vertex_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
