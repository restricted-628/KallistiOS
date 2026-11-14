/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#ifndef KOS_SH4DER_CA_H
#define KOS_SH4DER_CA_H
#include <sh4der_kos_image.h>
#include <sh4der/sh4der_ca.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SH4DER_KOS_CA_VERTS (2*(SH4DER_CA_MESH_X+1)*SH4DER_CA_MESH_Y)
typedef struct sh4der_kos_ca_plan {
    float rect[4], unit_push;
    float radius2[SH4DER_CA_MESH_Y+1][SH4DER_CA_MESH_X+1];
} sh4der_kos_ca_plan_t;
typedef struct sh4der_kos_ca_packet {
    pvr_poly_hdr_t header;
    pvr_vertex_t vertices[SH4DER_KOS_CA_VERTS];
} sh4der_kos_ca_packet_t;
/* Host/load or viewport change: prepare radial terms once. rect={x,y,w,h}.
   No GPU state or pointers; failure leaves output unchanged. */
int sh4der_kos_ca_prepare(sh4der_kos_ca_plan_t *out, const float rect[4]);
/* Three complete packets: GREEN(COPY/OP), RED(ADD/TR), BLUE(ADD/TR). Bind image
   with texel centers/BILINEAR/clamp once, any admitted content size/layout.
   strength[0,1]; z>0 and z_step>0 distinguish ordered channels. All checks before
   writing; capacity counts packets, out aligned32/disjoint. Return3 or -1/errno.
   Submit each complete packet through one sink and its matching list. Caller
   owns producers/readers, global stride and resize/rebind; no raw capture. */
int sh4der_kos_ca_packets(const sh4der_kos_ca_plan_t *plan,
    const sh4der_kos_image_t *image, float strength, float z, float z_step,
    sh4der_kos_ca_packet_t *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
