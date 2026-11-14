/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_lens.h>
#include <errno.h>
#include <math.h>
#include <string.h>
static int unit(float x) {return isfinite(x) && x>=0 && x<=1;}
int sh4der_kos_lens_vignette(const sh4der_lens_vig_t *v, float z,
    sh4der_lens_vtx_t *scratch, size_t scratch_count, pvr_vertex_t *out, size_t capacity) {
    if(!v || !scratch || !out || ((uintptr_t)out&31u) || v->spokes<3 ||
       v->spokes>SH4DER_LENS_SPOKES_MAX || !v->rings || v->rings>SH4DER_LENS_RINGS_MAX) {errno=EINVAL;return -1;}
    if(!isfinite(z) || z<=0 || !isfinite(v->w) || !isfinite(v->h) || v->w<=0 || v->h<=0 ||
       !isfinite(v->kx) || v->kx<=0 || !isfinite(v->inner) || v->inner<0 ||
       !isfinite(v->outer) || v->outer<=v->inner || !unit(v->strength) ||
       !unit(v->tint[0]) || !unit(v->tint[1]) || !unit(v->tint[2])) {errno=EDOM;return -1;}
    float dx=.5f*v->w*v->kx,dy=.5f*v->h,metric=dx*dx+dy*dy;
    if(!isfinite(metric) || metric<=0 || !isfinite(v->kx*v->kx) || v->kx*v->kx<=0 ||
       !isfinite(2/v->h) || !isfinite(1/(v->outer-v->inner))) {errno=ERANGE;return -1;}
    unsigned per=2*(v->spokes+1),n=(v->rings+1)*per;
    if(capacity<n || scratch_count<n) {errno=ENOSPC;return -1;}
    if(sh4der_lens_vig_mesh(v,scratch,n)!=n) {errno=ERANGE;return -1;}
    for(unsigned i=0;i<n;++i) if(!isfinite(scratch[i].x) || !isfinite(scratch[i].y)) {errno=ERANGE;return -1;}
    for(unsigned i=0;i<n;++i) out[i]=(pvr_vertex_t){
        .flags=i%per==per-1?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,
        .x=scratch[i].x,.y=scratch[i].y,.z=z,.argb=scratch[i].argb
    };
    return (int)n;
}
int sh4der_kos_lens_grain_bake(uint16_t *out, size_t capacity, uint32_t seed, float *mean) {
    if(!out || ((uintptr_t)out&1u) || !mean) {errno=EINVAL;return -1;}
    if(capacity<4096) {errno=ENOSPC;return -1;}
    unsigned sum=0;
    for(unsigned i=0;i<4096;++i) {unsigned level=sh4der_lens_grain_level(i,seed);sum+=level;out[i]=sh4der_lens_grain_argb4444(level);}
    *mean=sh4der_lens_grain_mean(sum,4096);return 0;
}
int sh4der_kos_lens_grain_bind(sh4der_kos_lens_grain_t *out,
    const pvr_txr_surface_t *surface, float amp, float mean) {
    if(!out || ((uintptr_t)out&31u) || !surface || surface->width!=64 || surface->height!=64 ||
       surface->format!=PVR_TXR_SURFACE_ARGB4444 || surface->layout!=PVR_TXR_SURFACE_LINEAR) {errno=EINVAL;return -1;}
    if(!unit(amp) || !unit(mean)) {errno=EDOM;return -1;}
    float kept=1-amp+amp*mean;
    if(kept<.5f) {errno=ERANGE;return -1;}
    float boost=sh4der_lens_grain_boost(amp,mean);
    sh4der_kos_lens_grain_t next={0};
    if(sh4der_kos_image_bind(&next.image,surface,64,64,PVR_FILTER_NEAREST,false,false)<0) return -1;
    pvr_poly_cxt_t c;
    /* Admitted nonmip linear surface: native header address is its storage. */
    pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444|PVR_TXRFMT_NONTWIDDLED,64,64,surface->vram,PVR_FILTER_NEAREST);
    c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;c.gen.specular=true;
    c.depth.comparison=PVR_DEPTHCMP_ALWAYS;c.depth.write=PVR_DEPTHWRITE_DISABLE;
    c.txr.env=PVR_TXRENV_MODULATE;c.txr.uv_clamp=PVR_UVCLAMP_NONE;
    c.blend.src=PVR_BLEND_DESTCOLOR;c.blend.dst=PVR_BLEND_ZERO;
    pvr_poly_compile(&next.modulate,&c);
    unsigned a=(unsigned)(amp*255+.5f),o=255-a;
    next.amplitude=0xff000000u|a*0x10101u;next.offset=0xff000000u|o*0x10101u;
    next.boost=sh4der_lens_argb_(1,boost,boost,boost);*out=next;return 0;
}
int sh4der_kos_lens_grain_packets(const sh4der_kos_lens_grain_t *g,
    const sh4der_kos_overlay_t *overlay, uint32_t frame, const float rect[4],
    float z, float z_step, sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(!g || !overlay || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(capacity<2) {errno=ENOSPC;return -1;}
    if(!isfinite(z_step) || z_step<=0 || !isfinite(z+z_step) || z+z_step<=z) {errno=ERANGE;return -1;}
    sh4der_kos_screen_packet_t next[2];
    if(sh4der_kos_image_quad(&g->image,SH4DER_KOS_IMAGE_MULTIPLY,rect,z,g->amplitude,&next[0])<0 ||
       sh4der_kos_overlay_quad(overlay,SH4DER_KOS_OVERLAY_BOOST,rect,z+z_step,g->boost,&next[1])<0) return -1;
    float uv[4];sh4der_lens_grain_uv(frame,rect[2],rect[3],uv);
    next[0].header=g->modulate;
    for(unsigned k=0;k<4;++k) {
        next[0].vertices[k].u=uv[k&1?2:0];next[0].vertices[k].v=uv[k&2?3:1];next[0].vertices[k].oargb=g->offset;
    }
    memcpy(out,next,sizeof(next));return 2;
}
int sh4der_kos_lens_hits(const sh4der_kos_overlay_t *overlay,
    const sh4der_lens_hit_t *slots, unsigned count, const float rect[4], float z,
    float z_step, sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(!overlay || !slots || count>SH4DER_LENS_HIT_MAX || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(!isfinite(z_step) || z_step<=0) {errno=EDOM;return -1;}
    sh4der_kos_screen_packet_t next[SH4DER_LENS_HIT_MAX];
    /* Also admits rectangle and depth when no live slots are emitted. */
    if(sh4der_kos_overlay_quad(overlay,SH4DER_KOS_OVERLAY_ALPHA,rect,z,0,&next[0])<0) return -1;
    unsigned n=0;float last=0;
    for(unsigned i=0;i<count;++i) {
        const sh4der_lens_hit_t *h=&slots[i];if(!h->on) continue;
        if(!isfinite(h->age) || h->age<0 || !isfinite(h->dx) || !isfinite(h->dy) ||
           fabsf(h->dx)>1.001f || fabsf(h->dy)>1.001f || fabsf(h->dx)+fabsf(h->dy)<.5f) {errno=EDOM;return -1;}
        float alpha=sh4der_lens_hit_alpha(h->age);if(alpha<=0) continue;
        if(n>=capacity) {errno=ENOSPC;return -1;}
        float depth=z+n*z_step;if(!isfinite(depth) || depth<=last) {errno=ERANGE;return -1;}last=depth;
        sh4der_lens_vtx_t q[4];sh4der_lens_hit_quad(h,640,480,alpha,q);
        next[n].header=overlay->headers[SH4DER_KOS_OVERLAY_ALPHA];
        for(unsigned k=0;k<4;++k) {
            float x=rect[0]+q[k].x*(rect[2]*(1.0f/640)),y=rect[1]+q[k].y*(rect[3]*(1.0f/480));
            if(!isfinite(x) || !isfinite(y)) {errno=ERANGE;return -1;}
            next[n].vertices[k]=(pvr_vertex_t){.flags=k==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,.x=x,.y=y,.z=depth,.argb=q[k].argb};
        }
        ++n;
    }
    memcpy(out,next,n*sizeof(*out));return (int)n;
}
int sh4der_kos_lens_flash(const sh4der_kos_overlay_t *overlay, float level,
    const float rgb[3], bool additive, const float rect[4], float z,
    sh4der_kos_screen_packet_t *out) {
    if(!rgb) {errno=EINVAL;return -1;}
    if(!unit(level) || !unit(rgb[0]) || !unit(rgb[1]) || !unit(rgb[2])) {errno=EDOM;return -1;}
    sh4der_kos_screen_packet_t next;
    if(sh4der_kos_overlay_quad(overlay,additive?SH4DER_KOS_OVERLAY_ADD:SH4DER_KOS_OVERLAY_ALPHA,rect,z,
        sh4der_lens_flash_argb(level,rgb,additive),&next)<0) return -1;
    if(!out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(level==0) return 0;
    *out=next;return 1;
}
