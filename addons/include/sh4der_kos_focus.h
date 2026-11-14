/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_FOCUS_H
#define KOS_SH4DER_FOCUS_H
#include <sh4der_kos_image.h>
#include <sh4der/sh4der_focus.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Prepared pure CPU state: immutable until reprepare, no borrowed mask pointer.
   Mask coordinates stay in the vendor's virtual 640x480 domain. */
typedef struct sh4der_kos_focus_plan {
    sh4der_focus_mask_t mask;
    float outer2[2], inverse[2];
} sh4der_kos_focus_plan_t;

/* Host/load or changed-mask admission; no repeated per-vertex reciprocals.
   Invalid/nonrepresentable masks return -1/errno leaving out intact. */
int sh4der_kos_focus_prepare(sh4der_kos_focus_plan_t *out,
    const sh4der_focus_mask_t *mask);

/* Edge-clamped 3x3 RGB565 blur, 80x60 ->128x64 (edge-filled padding).
   out capacity counts texels; input/output disjoint and naturally aligned16.
   1440 bytes automatic ring scratch, no static mutable state. Can run on host
   for static art; dynamic frame grids come from retained frame sampling.
   All size checks precede output, returns0 or -1/errno. */
int sh4der_kos_focus_blur(const uint16_t *grid, size_t count,
    uint16_t *out, size_t capacity);

/* Build408 vertices, EOL after each34-vertex strip, no submission. out aligned32
   and disjoint from inputs; insufficient capacity/error preserves output.
   Submit image.headers[ALPHA] then these vertices through one current/buffered
   TR sink. Bind the focus image once: RGB565 LINEAR128x64, content80x60,
   BILINEAR, centers=false, clamp=true. Upload with image_upload, retain all
   readers. This builder emits fixed UVs for that texture; it never inspects VRAM.
   rect={x,y,w,h}; mask remains virtual640x480. Static mesh can be host-prepared. */
int sh4der_kos_focus_mesh(const sh4der_kos_focus_plan_t *plan,
    const float rect[4], float z, pvr_vertex_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
