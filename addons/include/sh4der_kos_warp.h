/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_WARP_H
#define KOS_SH4DER_WARP_H
#include <sh4der_kos_image.h>
#include <sh4der/sh4der_warp.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host/load topology: integer-divided vendor virtual320x240 grid positions.
   Immutable after prepare; no integer division in the per-frame vertex loop. */
typedef struct sh4der_kos_warp_grid {
    unsigned nx, ny;
    float x[SH4DER_WARP_FINE_X+1], y[SH4DER_WARP_FINE_Y+1];
} sh4der_kos_warp_grid_t;
typedef struct sh4der_kos_warp_scratch {
    sh4der_warp_vtx_t rows[2][SH4DER_WARP_FINE_X+1];
} sh4der_kos_warp_scratch_t;
int sh4der_kos_warp_grid_prepare(sh4der_kos_warp_grid_t *out, unsigned nx, unsigned ny);

/* Return2*(nx+1)*ny vertices. Bind image with centers=true, BILINEAR/clamp,
   submit its COPY header then vertices through one OP sink. Caller owns scratch
   (1312B); all input/output/scratch ranges disjoint, output aligned32. Dynamic
   sources: valid kind, finite relevant coordinates, age>=0, strength[0,1].
   Haze/wobble phases must remain <=2^30 cycles (signed truncation domain);
   nonrepresentable ring distances/radius return ERANGE. All admission before
   output. Retained image and completed GPU readers follow image contract. */
int sh4der_kos_warp_mesh(const sh4der_kos_image_t *image,
    const sh4der_kos_warp_grid_t *grid, const sh4der_warp_frame_t *frame,
    const float rect[4], float z, sh4der_kos_warp_scratch_t *scratch,
    pvr_vertex_t *out, size_t capacity);

/* Radial blur:1..4 complete packets, first COPY(OP), rest ADD(TR). Integer
   weights sum255; caller routes lists and reserves increasing z/z_step for
   autosort. center normalized[0,1], strength[0,1]. Bind BILINEAR, centers=true.
   Returns copies or -1 without writes. Static packets may be built on host. */
int sh4der_kos_warp_blur(const sh4der_kos_image_t *image,
    const float rect[4], const float center[2], float strength, unsigned copies,
    float z, float z_step, sh4der_kos_screen_packet_t *out, size_t capacity);

#define SH4DER_KOS_WARP_PIXEL_VERTS 1200
/*300 independent pixel-block quads; bind NEAREST/centers/clamp, COPY(OP).
   Fixed16x16 virtual pixels per block, independent of destination resolution. */
int sh4der_kos_warp_pixel(const sh4der_kos_image_t *image,
    const float rect[4], float z, pvr_vertex_t *out, size_t capacity);

/* Host-bake8x8 ARGB4444 with sh4der_warp_dither_tile; bind TWIDDLED, NEAREST,
   centers=false, clamp=false; upload through image_upload. Complete multiply
   TR packet repeats40x30 over virtual320x240; output aligned32/disjoint.
   No implicit allocation, stride change, draw or lifetime tracking. */
int sh4der_kos_warp_dither(const sh4der_kos_image_t *tile,
    const float rect[4], float z, sh4der_kos_screen_packet_t *out);
#ifdef __cplusplus
}
#endif
#endif
