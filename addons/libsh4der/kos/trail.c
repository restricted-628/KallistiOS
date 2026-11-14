/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_trail.h>
#include <errno.h>
#include <math.h>
int sh4der_kos_trail_bind(sh4der_kos_trail_history_t *out,
    const sh4der_kos_image_t *image,unsigned w,unsigned h,uint64_t generation) {
    if(!out || !image || !image->surface || !w || !h || !generation) {errno=EINVAL;return -1;}
    const pvr_txr_surface_t *s=image->surface;
    if(s->format!=PVR_TXR_SURFACE_RGB565 || s->mipmapped || w>s->width || h>s->height ||
       (s->layout!=PVR_TXR_SURFACE_LINEAR && s->layout!=PVR_TXR_SURFACE_STRIDE)) {errno=EINVAL;return -1;}
    pvr_ptr_t address;
    if(pvr_txr_surface_get_texture_address(s,&address)<0) return -1;
    *out=(sh4der_kos_trail_history_t){.image=image,.address=address,.width=w,.height=h,.stride=s->width,.id={generation,0}};
    return 0;
}
void sh4der_kos_trail_invalidate(sh4der_kos_trail_history_t *h) {if(h)h->id.render=0;}
int sh4der_kos_trail_publish(sh4der_kos_trail_history_t *h,
    const pvr_render_ticket_t *t,uint64_t generation) {
    if(!h || !t || !h->image || !generation || generation!=h->id.generation ||
       !t->id || t->id<=h->last_render || !t->to_texture || t->target!=h->address ||
       t->width!=h->width || t->height!=h->height || t->stride!=h->stride) {errno=EINVAL;return -1;}
    pvr_render_stage_t stage;
    if(pvr_render_ticket_get_stage(t,&stage)<0) return -1;
    h->id.render=t->id;h->last_render=t->id;return 0;
}
int sh4der_kos_trail_packets(const sh4der_kos_trail_history_t *h,
    sh4der_kos_frame_id_t expected,pvr_ptr_t target,const sh4der_kos_overlay_t *overlay,
    const sh4der_trail_frame_t *f,const float rect[4],float z,float dz,
    sh4der_kos_screen_packet_t *out,size_t capacity) {
    if(!f || !rect || !out || ((uintptr_t)out&31u) || !overlay) {errno=EINVAL;return -1;}
    if(!isfinite(f->weight) || f->weight<0 || f->weight>1 || !isfinite(f->copy_w) || f->copy_w<0 || f->copy_w>1) {errno=EDOM;return -1;}
    if(f->cut || f->weight==0) return 0;
    if(!h || !h->image || !h->id.render || expected.generation!=h->id.generation || expected.render!=h->id.render) {errno=ESTALE;return -1;}
    if(!target || target==h->address || f->copies<1 || f->copies>SH4DER_TRAIL_COPIES) {errno=EINVAL;return -1;}
    if(fabsf(f->copy_w*f->copies-f->weight)>1e-5f) {errno=EDOM;return -1;}
    for(unsigned k=0;k<4;++k)if(!isfinite(rect[k])) {errno=EDOM;return -1;}
    if(rect[2]!=h->width || rect[3]!=h->height || !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3]) ||
       !isfinite(z) || z<=0 || !isfinite(dz) || dz<=0) {errno=EDOM;return -1;}
    float depth=z;
    for(int i=0;i<f->copies;++i) {
        float next=z+(i+1)*dz;
        if(!isfinite(next) || next<=depth || f->dx[i]<-(int)h->width || f->dx[i]>(int)h->width ||
           f->dy[i]<-(int)h->height || f->dy[i]>(int)h->height ||
           !isfinite(rect[0]+rect[2]+f->dx[i]) || !isfinite(rect[1]+rect[3]+f->dy[i])) {errno=ERANGE;return -1;}
        depth=next;
    }
    if(capacity<(size_t)f->copies+1) {errno=ENOSPC;return -1;}
    sh4der_kos_overlay_quad(overlay,SH4DER_KOS_OVERLAY_MULTIPLY,rect,z,sh4der_taa_keep_argb(f->weight),out);
    for(int i=0;i<f->copies;++i) {
        float shifted[4]={rect[0]+f->dx[i],rect[1]+f->dy[i],rect[2],rect[3]};
        sh4der_kos_image_quad(h->image,SH4DER_KOS_IMAGE_ADD,shifted,z+(i+1)*dz,sh4der_taa_add_argb(f->copy_w),out+i+1);
    }
    return f->copies+1;
}
int sh4der_kos_taa_packets(const sh4der_kos_trail_history_t *h,
    sh4der_kos_frame_id_t expected,pvr_ptr_t target,const sh4der_kos_overlay_t *overlay,
    const sh4der_taa_frame_t *f,const float rect[4],float z,float dz,
    sh4der_kos_screen_packet_t *out,size_t capacity) {
    if(!f){errno=EINVAL;return -1;}
    sh4der_trail_frame_t t={.weight=f->weight,.copy_w=f->weight,.copies=1,
        .dx={f->copy_px[0]},.dy={f->copy_px[1]},.cut=f->cut};
    return sh4der_kos_trail_packets(h,expected,target,overlay,&t,rect,z,dz,out,capacity);
}
