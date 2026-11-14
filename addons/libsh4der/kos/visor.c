/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_visor.h>
#include <errno.h>
#include <math.h>
#include <string.h>
int sh4der_kos_visor_prepare(sh4der_kos_visor_plan_t *out, int mode) {
    if(!out || ((uintptr_t)out&31u) || mode<SH4DER_VISOR_OFF || mode>=SH4DER_VISOR_MODES) {errno=EINVAL;return -1;}
    sh4der_focus_mask_t mask={.shape=SH4DER_FOCUS_DISC,.cx=320,.cy=240,.hw=SH4DER_VISOR_VIG_R,.hh=0,.feather=SH4DER_VISOR_VIG_FEATHER};
    sh4der_kos_focus_plan_t focus;
    const float rect[4]={0,0,640,480};
    /* Fixed valid defaults: preparation has no dependent runtime failure. */
    if(sh4der_kos_focus_prepare(&focus,&mask)<0) return -1;
    if(mode==SH4DER_VISOR_OFF) out->description=sh4der_visor_presets[0];
    else sh4der_visor_preset(mode,&out->description);
    sh4der_visor_lut(out->lut);
    sh4der_visor_noise_grid(out->grain,out->description.grain);
    sh4der_visor_scan_grid(out->scan,out->description.scanline);
    return sh4der_kos_focus_mesh(&focus,rect,1,out->vignette,SH4DER_FOCUS_VERTS)<0?-1:0;
}
int sh4der_kos_visor_thermal(const sh4der_kos_visor_plan_t *p,
    const uint16_t *grid, size_t count, uint16_t *out, size_t capacity) {
    if(!p || !grid || !out || ((uintptr_t)grid&1u) || ((uintptr_t)out&1u)) {errno=EINVAL;return -1;}
    if(count<4800 || capacity<8192) {errno=ENOSPC;return -1;}
    sh4der_visor_thermal_grid(p->lut,grid,out);return 0;
}
int sh4der_kos_visor_packets(const sh4der_kos_visor_plan_t *p,
    const sh4der_kos_overlay_t *overlay, const sh4der_kos_image_t *thermal,
    const sh4der_kos_image_t *grain, const sh4der_kos_image_t *scan,
    float amount, uint32_t frame, const float rect[4], float z, float z_step,
    sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(!p || !overlay || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(!isfinite(amount) || amount<0 || amount>1 || !isfinite(z_step) || z_step<=0) {errno=EDOM;return -1;}
    sh4der_kos_screen_packet_t next[SH4DER_KOS_VISOR_PACKETS];
    if(sh4der_kos_overlay_quad(overlay,SH4DER_KOS_OVERLAY_ALPHA,rect,z,0,&next[0])<0) return -1;
    if(amount==0) return 0;
    unsigned n=0;
    if(p->description.thermal) {
        uint32_t alpha=(uint32_t)(amount*255+.5f);
        if(sh4der_kos_image_quad(thermal,SH4DER_KOS_IMAGE_ALPHA,rect,z,(alpha<<24)|0xffffff,&next[n++])<0) return -1;
    } else {
        sh4der_look_grade_t grade;sh4der_look_layer_t layers[SH4DER_LOOK_LAYERS];
        sh4der_visor_grade(&p->description,amount,&grade);
        int count=sh4der_look_solve(&grade,layers);
        for(int i=0;i<count;++i) {
            if(sh4der_kos_overlay_quad(overlay,(sh4der_kos_overlay_mode_t)layers[i].kind,rect,z+n*z_step,sh4der_look_argb(&layers[i]),&next[n])<0) return -1;
            ++n;
        }
    }
    if(grain && p->description.grain>0) {
        if(sh4der_kos_image_quad(grain,SH4DER_KOS_IMAGE_MULTIPLY,rect,z+n*z_step,0xffffffff,&next[n])<0) return -1;
        float du=(frame%97u)*SH4DER_VISOR_GRAIN_SPEED_U,dv=(frame%89u)*SH4DER_VISOR_GRAIN_SPEED_V;
        for(unsigned k=0;k<4;++k) {next[n].vertices[k].u=du+(k&1?20:0);next[n].vertices[k].v=dv+(k&2?15:0);}
        ++n;
    }
    if(scan && p->description.scanline>0) {
        if(sh4der_kos_image_quad(scan,SH4DER_KOS_IMAGE_MULTIPLY,rect,z+n*z_step,0xffffffff,&next[n])<0) return -1;
        for(unsigned k=0;k<4;++k) {next[n].vertices[k].u=k&1?1:0;next[n].vertices[k].v=k&2?30:0;}
        ++n;
    }
    if(capacity<n) {errno=ENOSPC;return -1;}
    for(unsigned i=1;i<n;++i) if(next[i].vertices[0].z<=next[i-1].vertices[0].z) {errno=ERANGE;return -1;}
    memcpy(out,next,n*sizeof(*out));return (int)n;
}
int sh4der_kos_visor_vignette(const sh4der_kos_visor_plan_t *p,
    float amount, const float rect[4], float z, pvr_vertex_t *out, size_t capacity) {
    if(!p || !rect || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(!isfinite(amount) || amount<0 || amount>1 || !isfinite(z) || z<=0) {errno=EDOM;return -1;}
    for(unsigned i=0;i<4;++i) if(!isfinite(rect[i])) {errno=EDOM;return -1;}
    if(rect[2]<=0 || rect[3]<=0 || !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3])) {errno=EDOM;return -1;}
    if(amount==0 || p->description.vignette==0) return 0;
    if(capacity<SH4DER_FOCUS_VERTS) {errno=ENOSPC;return -1;}
    float sx=rect[2]*(1.0f/640),sy=rect[3]*(1.0f/480);
    for(unsigned i=0;i<SH4DER_FOCUS_VERTS;++i) {
        out[i]=p->vignette[i];out[i].x=rect[0]+out[i].x*sx;out[i].y=rect[1]+out[i].y*sy;out[i].z=z;
        out[i].u=out[i].v=0;out[i].argb=sh4der_visor_vignette_argb(p->vignette[i].argb,p->description.vignette,amount);
    }
    return SH4DER_FOCUS_VERTS;
}
