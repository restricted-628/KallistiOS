/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_shafts.h>
#include <errno.h>
#include <math.h>
#include <string.h>
int sh4der_kos_shafts_prepare(sh4der_kos_shafts_plan_t *out, const float sun[2]) {
    if(!out || !sun) {errno=EINVAL;return -1;}
    if(!isfinite(sun[0]) || !isfinite(sun[1]) || sun[0]<0 || sun[0]>1 || sun[1]<0 || sun[1]>1) {errno=EDOM;return -1;}
    out->sun[0]=sun[0];out->sun[1]=sun[1];
    for(unsigned y=0;y<SH4DER_GLOW_GRID_Y;++y) for(unsigned x=0;x<SH4DER_GLOW_GRID_X;++x)
        out->near[y*SH4DER_GLOW_GRID_X+x]=sh4der_shafts_near((x+.5f)*(1.0f/SH4DER_GLOW_GRID_X),(y+.5f)*(1.0f/SH4DER_GLOW_GRID_Y),sun[0],sun[1]);
    return 0;
}
int sh4der_kos_shafts_map(sh4der_kos_shafts_stage_t *out,
    const sh4der_kos_shafts_plan_t *plan, const uint16_t *grid, size_t count,
    float strength, float facing, sh4der_kos_frame_id_t source,
    const sh4der_kos_shafts_stage_t *previous) {
    if(!out || ((uintptr_t)out&31u) || !plan || !grid || ((uintptr_t)grid&1u) ||
       !source.generation || !source.render || (previous &&
       (previous->source.generation!=source.generation || !previous->source.render || previous->source.render>=source.render))) {errno=EINVAL;return -1;}
    if(count<SH4DER_KOS_FRAME_SAMPLES) {errno=ENOSPC;return -1;}
    if(!isfinite(strength) || strength<0 || strength>3 || !isfinite(facing) || facing < -1 || facing>1) {errno=EDOM;return -1;}
    float depth=sh4der_shafts_depth(strength,sh4der_shafts_toward(facing));
    for(unsigned y=0;y<SH4DER_GLOW_GRID_Y;++y) {
        for(unsigned x=0;x<SH4DER_GLOW_GRID_X;++x) {
            unsigned dst=y*SH4DER_GLOW_TEX_W+x,src=y*SH4DER_GLOW_GRID_X+x;
            float luma=sh4der_glow_luma8_(grid[src])*(1.0f/255);
            float cast=sh4der_shafts_cast(luma)*plan->near[src];
            out->pixels[dst]=sh4der_shafts_texel(previous?previous->pixels[dst]:0xffff,1-depth*cast);
        }
        memset(out->pixels+y*SH4DER_GLOW_TEX_W+SH4DER_GLOW_GRID_X,255,
            (SH4DER_GLOW_TEX_W-SH4DER_GLOW_GRID_X)*sizeof(uint16_t));
    }
    memset(out->pixels+SH4DER_GLOW_GRID_Y*SH4DER_GLOW_TEX_W,255,
        (SH4DER_GLOW_TEX_H-SH4DER_GLOW_GRID_Y)*SH4DER_GLOW_TEX_W*sizeof(uint16_t));
    out->source=source;return 0;
}
int sh4der_kos_shafts_mesh(const sh4der_kos_shafts_plan_t *p,
    const float rect[4], float z, float scale, pvr_vertex_t *out, size_t capacity) {
    if(!p || !rect || !out || ((uintptr_t)out&31u)) {errno=EINVAL;return -1;}
    if(capacity<SH4DER_KOS_SHAFTS_VERTS) {errno=ENOSPC;return -1;}
    for(unsigned i=0;i<4;++i) if(!isfinite(rect[i])) {errno=EDOM;return -1;}
    if(!isfinite(z) || z<=0 || !isfinite(scale) || scale<0 || scale>1 || rect[2]<=0 || rect[3]<=0 ||
       !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3])) {errno=EDOM;return -1;}
    unsigned n=0;
    for(unsigned y=0;y<SH4DER_GLOW_MESH_Y;++y) for(unsigned x=0;x<=SH4DER_GLOW_MESH_X;++x) for(unsigned k=0;k<2;++k) {
        float u=x*(1.0f/SH4DER_GLOW_MESH_X),v=(y+k)*(1.0f/SH4DER_GLOW_MESH_Y);
        pvr_vertex_t *a=&out[n++];
        *a=(pvr_vertex_t){.flags=x==SH4DER_GLOW_MESH_X && k?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,
            .x=rect[0]+u*rect[2],.y=rect[1]+v*rect[3],.z=z,.argb=0xffffffff};
        sh4der_glow_ray_uv(scale,p->sun[0],p->sun[1],u,v,&a->u,&a->v);
    }
    return (int)n;
}
