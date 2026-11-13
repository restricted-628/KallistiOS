/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_SCREEN_H
#define KOS_SH4DER_SCREEN_H
#include <dc/pvr.h>
#include <sh4der/sh4der_look.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Complete untextured strip, header and vertices on the SAME destination.
   Align caller arrays to 32 bytes. After opening TR, submit each whole packet
   through pvr_prim or pvr_list_prim as appropriate. No direct-SQ bypass.
   Caller owns sorting mode: increasing z orders layers far-to-near with TR
   autosort; in manual mode submit in array order. Put z0 in front of the scene.
   Do not mix these depths with interleaved unrelated transparent overlays. */
typedef struct sh4der_kos_screen_packet {
    pvr_poly_hdr_t header;
    pvr_vertex_t vertices[4];
} sh4der_kos_screen_packet_t;

/* Return 0..SH4DER_LOOK_LAYERS packets, or -1/errno with output unchanged.
   Inputs must not overlap output. Rect is x,y,width,height in screen pixels;
   z is reciprocal camera depth.
   Frame-dependent inputs are checked once per build. No scene/list/VRAM/fog
   ownership. Fog disabled, depth ALWAYS, no depth writes, no culling.
   This is grade rendering, not automatic exposure or framebuffer capture. */
int sh4der_kos_grade_packets(const sh4der_look_grade_t *grade,
    const float rect[4], float z0, float z_step,
    sh4der_kos_screen_packet_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
