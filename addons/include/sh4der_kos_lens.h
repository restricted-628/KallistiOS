/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_LENS_H
#define KOS_SH4DER_LENS_H
#include <sh4der_kos_image.h>
#include <sh4der_kos_overlay.h>
#include <sh4der/sh4der_lens.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_LENS_VIG_MAX ((SH4DER_LENS_RINGS_MAX+1)*2*(SH4DER_LENS_SPOKES_MAX+1))
/* Host/load vignette bake: output coordinates use shape.w/h at origin, z>0.
   Caller scratch holds at least required_count public lens vertices; output
   capacity counts PVR vertices. Both buffers disjoint, output aligned32.
   Scratch may change on error, output remains intact. Submit overlay MULTIPLY
   header and vertices through one TR sink; reuse until shape/depth changes. */
int sh4der_kos_lens_vignette(const sh4der_lens_vig_t *shape, float z,
    sh4der_lens_vtx_t *scratch, size_t scratch_count, pvr_vertex_t *out, size_t capacity);

/* Host/load deterministic64x64 LINEAR ARGB4444 grain bake, returns mean in[0,1].
   No mutable seed state; capacities in texels, disjoint output and mean. */
int sh4der_kos_lens_grain_bake(uint16_t *out, size_t capacity, uint32_t seed, float *mean);
typedef struct sh4der_kos_lens_grain {
    sh4der_kos_image_t image;
    pvr_poly_hdr_t modulate;
    uint32_t amplitude, offset, boost;
} sh4der_kos_lens_grain_t;
/* Admit64x64 LINEAR ARGB4444 once. amp/mean in[0,1], required compensation <=2x
   (larger gain returns ERANGE). Compile offset-color modulation header, pack
   constant weights. Failure preserves out; no VRAM change. Upload baked tile
   with image_upload(&grain.image,...), retain all readers before update/release. */
int sh4der_kos_lens_grain_bind(sh4der_kos_lens_grain_t *out,
    const pvr_txr_surface_t *surface, float amplitude, float mean);
/* Two TR packets: textured multiply with offset, then untextured mean BOOST.
   Caller reserves distinct z/z_step; capacity>=2, out aligned32/disjoint.
   Frame changes UV phase only; header, tile and packed weights stay prepared. */
int sh4der_kos_lens_grain_packets(const sh4der_kos_lens_grain_t *grain,
    const sh4der_kos_overlay_t *overlay, uint32_t frame, const float rect[4],
    float z, float z_step, sh4der_kos_screen_packet_t *out, size_t capacity);
/* Caller owns public hit_add/hit_tick state (up to4 slots). Live slots must have
   finite unit direction and age>=0. Geometry uses virtual640x480 scaled to rect.
   Return number of visible packets, all ALPHA/TR, ordered distinct depths.
   Failure preserves output. No slots/state are changed. */
int sh4der_kos_lens_hits(const sh4der_kos_overlay_t *overlay,
    const sh4der_lens_hit_t *slots, unsigned count, const float rect[4], float z,
    float z_step, sh4der_kos_screen_packet_t *out, size_t capacity);
/* Caller owns public flash_hit/flash_step envelope; level/rgb in[0,1]. Return
   zero without writes if level0, else one complete ADD or ALPHA packet. */
int sh4der_kos_lens_flash(const sh4der_kos_overlay_t *overlay, float level,
    const float rgb[3], bool additive, const float rect[4], float z,
    sh4der_kos_screen_packet_t *out);
#ifdef __cplusplus
}
#endif
#endif
