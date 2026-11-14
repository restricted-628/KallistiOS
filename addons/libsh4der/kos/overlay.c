/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_overlay.h>
#include <errno.h>
#include <math.h>
int sh4der_kos_overlay_prepare(sh4der_kos_overlay_t *out) {
    if(!out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    static const pvr_blend_mode_t src[5]={PVR_BLEND_ZERO,PVR_BLEND_DESTCOLOR,PVR_BLEND_ONE,PVR_BLEND_INVDESTCOLOR,PVR_BLEND_SRCALPHA};
    static const pvr_blend_mode_t dst[5]={PVR_BLEND_DESTCOLOR,PVR_BLEND_ONE,PVR_BLEND_ONE,PVR_BLEND_ZERO,PVR_BLEND_INVSRCALPHA};
    for(unsigned i=0;i<5;++i) {
        pvr_poly_cxt_t c;pvr_poly_cxt_col(&c,PVR_LIST_TR_POLY);
        c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;
        c.depth.comparison=PVR_DEPTHCMP_ALWAYS;c.depth.write=PVR_DEPTHWRITE_DISABLE;
        c.blend.src=src[i];c.blend.dst=dst[i];pvr_poly_compile(&out->headers[i],&c);
    }
    return 0;
}
int sh4der_kos_overlay_quad(const sh4der_kos_overlay_t *overlay,
    sh4der_kos_overlay_mode_t mode, const float rect[4], float z, uint32_t color,
    sh4der_kos_screen_packet_t *out) {
    if(!overlay || !rect || !out || ((uintptr_t)out&31u) || mode<0 || mode>SH4DER_KOS_OVERLAY_ALPHA) {errno=EINVAL;return -1;}
    for(unsigned i=0;i<4;++i) if(!isfinite(rect[i])) {errno=EDOM;return -1;}
    if(!isfinite(z) || z<=0 || rect[2]<=0 || rect[3]<=0 || !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3])) {errno=EDOM;return -1;}
    out->header=overlay->headers[mode];
    for(unsigned k=0;k<4;++k) out->vertices[k]=(pvr_vertex_t){
        .flags=k==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,.x=rect[0]+(k&1?rect[2]:0),
        .y=rect[1]+(k&2?rect[3]:0),.z=z,.argb=color
    };
    return 0;
}
