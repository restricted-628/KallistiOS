/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_ca.h>
#include <errno.h>
#include <math.h>
#include <string.h>
_Static_assert(sizeof(sh4der_kos_ca_packet_t)==32*(1+SH4DER_KOS_CA_VERTS),"complete CA packet");
int sh4der_kos_ca_prepare(sh4der_kos_ca_plan_t *out, const float rect[4]) {
    if(!out || !rect) {errno=EINVAL;return -1;}
    for(unsigned i=0;i<4;++i) if(!isfinite(rect[i])) {errno=EDOM;return -1;}
    if(rect[2]<=0 || rect[3]<=0 || !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3])) {errno=EDOM;return -1;}
    float diagonal2=rect[2]*rect[2]+rect[3]*rect[3];
    if(!isfinite(diagonal2) || diagonal2<=0) {errno=ERANGE;return -1;}
    float inverse=sh4der_div_(4,diagonal2);
    sh4der_kos_ca_plan_t next;
    memcpy(next.rect,rect,sizeof(next.rect));
    next.unit_push=sh4der_ca_push(SH4DER_CA_GREEN,1,rect[2],rect[3]);
    if(!isfinite(inverse) || !isfinite(next.unit_push) || !isfinite(2*next.unit_push)) {errno=ERANGE;return -1;}
    for(unsigned y=0;y<=SH4DER_CA_MESH_Y;++y) for(unsigned x=0;x<=SH4DER_CA_MESH_X;++x)
        next.radius2[y][x]=sh4der_ca_r2(x*(1.0f/SH4DER_CA_MESH_X),y*(1.0f/SH4DER_CA_MESH_Y),rect[2],rect[3],inverse);
    *out=next;return 0;
}
int sh4der_kos_ca_packets(const sh4der_kos_ca_plan_t *p,
    const sh4der_kos_image_t *image, float strength, float z, float z_step,
    sh4der_kos_ca_packet_t *out, size_t capacity) {
    if(!p || !image || !image->surface || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(capacity<3) {errno=ENOSPC;return -1;}
    if(!isfinite(strength) || strength<0 || strength>1 || !isfinite(z) || z<=0 || !isfinite(z_step) || z_step<=0) {errno=EDOM;return -1;}
    float depth[3]={z,z+z_step,z+2*z_step};
    if(!isfinite(depth[2]) || depth[1]<=depth[0] || depth[2]<=depth[1]) {errno=ERANGE;return -1;}
    static const int channel[3]={SH4DER_CA_GREEN,SH4DER_CA_RED,SH4DER_CA_BLUE};
    for(unsigned c=0;c<3;++c) {
        out[c].header=image->headers[c?SH4DER_KOS_IMAGE_ADD:SH4DER_KOS_IMAGE_COPY];
        float push=p->unit_push*strength*(c==0?1:c==1?2:0);
        unsigned n=0;
        for(unsigned y=0;y<SH4DER_CA_MESH_Y;++y) for(unsigned x=0;x<=SH4DER_CA_MESH_X;++x) for(unsigned k=0;k<2;++k) {
            float u=x*(1.0f/SH4DER_CA_MESH_X),v=(y+k)*(1.0f/SH4DER_CA_MESH_Y),pu,pv;
            sh4der_ca_sample(push,p->radius2[y+k][x],u,v,&pu,&pv);
            out[c].vertices[n++]=(pvr_vertex_t){
                .flags=x==SH4DER_CA_MESH_X && k?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,
                .x=p->rect[0]+u*p->rect[2],.y=p->rect[1]+v*p->rect[3],.z=depth[c],
                .u=image->uv[0]+sh4der_sat_(pu)*image->uv[2],
                .v=image->uv[1]+sh4der_sat_(pv)*image->uv[3],.argb=sh4der_ca_argb(channel[c])
            };
        }
    }
    return 3;
}
