/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_OVERLAY_H
#define KOS_SH4DER_OVERLAY_H
#include <sh4der_kos_screen.h>
#ifdef __cplusplus
extern "C" {
#endif
/* First four match SH4DER_LOOK_* kinds; ALPHA extends them for lens/visor. */
typedef enum sh4der_kos_overlay_mode {
    SH4DER_KOS_OVERLAY_MULTIPLY, SH4DER_KOS_OVERLAY_BOOST,
    SH4DER_KOS_OVERLAY_ADD, SH4DER_KOS_OVERLAY_INVERT, SH4DER_KOS_OVERLAY_ALPHA
} sh4der_kos_overlay_mode_t;
typedef struct sh4der_kos_overlay { pvr_poly_hdr_t headers[5]; } sh4der_kos_overlay_t;
/* Compile invariant TR/fog-off/ALWAYS/no-write headers once per PVR setup. */
int sh4der_kos_overlay_prepare(sh4der_kos_overlay_t *out);
/* Build a complete untextured quad, caller TR sink/order, packed color.
   out aligned32/disjoint; invalid parameters leave it unchanged. */
int sh4der_kos_overlay_quad(const sh4der_kos_overlay_t *overlay,
    sh4der_kos_overlay_mode_t mode, const float rect[4], float z, uint32_t color,
    sh4der_kos_screen_packet_t *out);
/* Dynamic grade variant of screen.h's builder, reusing prepared headers.
   Same solver, validation, capacity and ordering; no per-frame header compile. */
int sh4der_kos_grade_packets_cached(const sh4der_kos_overlay_t *overlay,
    const sh4der_look_grade_t *grade,const float rect[4],float z,float dz,
    sh4der_kos_screen_packet_t *out,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
