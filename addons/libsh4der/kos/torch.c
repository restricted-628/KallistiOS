/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_torch.h>
#include <errno.h>
#include <math.h>
#include <string.h>
static int unit(float x) {return isfinite(x) && x>=0 && x<=1;}
int sh4der_kos_torch_prepare(sh4der_kos_torch_plan_t *out,
    const sh4der_kos_torch_shape_t *s, sh4der_torch_vert_t *scratch, size_t capacity) {
    if(!out || !s || !scratch) {errno=EINVAL;return -1;}
    if(capacity<SH4DER_TORCH_VERTS) {errno=ENOSPC;return -1;}
    if(!isfinite(s->cx) || !isfinite(s->cy) || !isfinite(s->rx) || !isfinite(s->ry) ||
       s->rx<=0 || s->ry<=0 || !unit(s->tint_g) || !unit(s->tint_b) || !unit(s->hot_u) ||
       !isfinite(s->umax) || s->umax<SH4DER_TORCH_EDGE_OUT) {errno=EDOM;return -1;}
    if(!isfinite(fabsf(s->cx)+s->rx*s->umax) || !isfinite(fabsf(s->cy)+s->ry*s->umax)) {errno=ERANGE;return -1;}
    sh4der_torch_carve_mesh_to(scratch,s->cx,s->cy,s->rx,s->ry,0,s->umax);
    memcpy(out->geometry,scratch,SH4DER_KOS_TORCH_CARVE_VERTS*sizeof(*scratch));
    sh4der_torch_boost_mesh_core(scratch,s->cx,s->cy,s->rx,s->ry,0,s->tint_g,s->tint_b,s->hot_u);
    memcpy(out->geometry+SH4DER_KOS_TORCH_CARVE_VERTS,scratch,SH4DER_KOS_TORCH_BOOST_VERTS*sizeof(*scratch));
    for(unsigned b=0;b<SH4DER_TORCH_BANDS;++b) for(unsigned k=0;k<2;++k) {
        unsigned i=b+k;
        float u=i?SH4DER_TORCH_EDGE_OUT+(i-1)*(s->umax-SH4DER_TORCH_EDGE_OUT)*(1.0f/(SH4DER_TORCH_BANDS-1)):SH4DER_TORCH_EDGE_IN;
        out->carve[b*2+k]=sh4der_torch_smooth_(u,SH4DER_TORCH_EDGE_IN,SH4DER_TORCH_EDGE_OUT);
    }
    for(unsigned b=0;b<SH4DER_TORCH_RINGS;++b) for(unsigned k=0;k<2;++k)
        out->boost[b*2+k]=sh4der_torch_boost_at_u((b+k)*(1.0f/SH4DER_TORCH_RINGS),1,s->hot_u);
    out->tint_g=s->tint_g;out->tint_b=s->tint_b;return 0;
}
static uint32_t color(float r,float g,float b) {
    return 0xff000000u|((uint32_t)(sh4der_sat_(r)*255+.5f)<<16)|((uint32_t)(sh4der_sat_(g)*255+.5f)<<8)|(uint32_t)(sh4der_sat_(b)*255+.5f);
}
int sh4der_kos_torch_mesh(const sh4der_kos_torch_plan_t *p,
    float power,float carve,float z_carve,float z_boost,pvr_vertex_t *out,size_t capacity) {
    if(!p || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(!unit(power) || !unit(carve) || !isfinite(z_carve) || z_carve<=0 || !isfinite(z_boost) || z_boost<=z_carve) {errno=EDOM;return -1;}
    if(power==0 && carve==0) return 0;
    if(capacity<SH4DER_TORCH_VERTS) {errno=ENOSPC;return -1;}
    unsigned n=0;
    for(unsigned layer=0;layer<2;++layer) {
        unsigned bands=layer?SH4DER_TORCH_RINGS:SH4DER_TORCH_BANDS;
        for(unsigned b=0;b<bands;++b) {
            uint32_t colors[2];
            for(unsigned k=0;k<2;++k) {
                float v=layer?p->boost[b*2+k]*power:1-carve*(1-SH4DER_TORCH_MUL_OUT)*p->carve[b*2+k];
                colors[k]=color(v,layer?v*p->tint_g:v,layer?v*p->tint_b:v);
            }
            for(unsigned j=0;j<=SH4DER_TORCH_SPOKES;++j) for(unsigned k=0;k<2;++k) {
                const sh4der_torch_vert_t *v=&p->geometry[n];
                out[n++]=(pvr_vertex_t){.flags=v->eol?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,
                    .x=v->x,.y=v->y,.z=layer?z_boost:z_carve,.argb=colors[k]};
            }
        }
    }
    return (int)n;
}
int sh4der_kos_torch_fog_prepare(sh4der_kos_torch_fog_plan_t *out,
    const float *base,size_t count,float far_depth) {
    if(!out || !base) {errno=EINVAL;return -1;}
    if(count<SH4DER_KOS_TORCH_FOG_ENTRIES) {errno=ENOSPC;return -1;}
    if(!isfinite(far_depth) || far_depth<=0) {errno=EDOM;return -1;}
    for(unsigned i=0;i<SH4DER_KOS_TORCH_FOG_ENTRIES;++i) if(!unit(base[i])) {errno=EDOM;return -1;}
    for(unsigned i=0;i<SH4DER_KOS_TORCH_FOG_ENTRIES;++i) {
        out->base[i]=base[i];
        out->reach[i]=sh4der_torch_smooth_(sh4der_torch_entry_depth(i,far_depth),SH4DER_TORCH_FOG_NEAR,SH4DER_TORCH_FOG_FAR);
    }
    out->far_depth=far_depth;return 0;
}
int sh4der_kos_torch_fog(const sh4der_kos_torch_fog_plan_t *p,float reach,float *out,size_t capacity) {
    if(!p || !out) {errno=EINVAL;return -1;}
    if(capacity<SH4DER_KOS_TORCH_FOG_ENTRIES) {errno=ENOSPC;return -1;}
    if(!unit(reach)) {errno=EDOM;return -1;}
    for(unsigned i=0;i<SH4DER_KOS_TORCH_FOG_ENTRIES;++i) out[i]=1-(1-p->base[i])*(1-reach*p->reach[i]);
    return 0;
}
