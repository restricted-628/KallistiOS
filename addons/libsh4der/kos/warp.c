/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <sh4der_kos_warp.h>
#include <errno.h>
#include <math.h>
#include <string.h>

int sh4der_kos_warp_grid_prepare(sh4der_kos_warp_grid_t *out, unsigned nx, unsigned ny) {
    if(!out || !nx || !ny || nx>SH4DER_WARP_FINE_X || ny>SH4DER_WARP_FINE_Y) {
        errno=EINVAL; return -1;
    }
    sh4der_kos_warp_grid_t next={.nx=nx,.ny=ny};
    for(unsigned i=0;i<=nx;++i) next.x[i]=(float)((i*320)/nx);
    for(unsigned j=0;j<=ny;++j) next.y[j]=(float)((j*240)/ny);
    *out=next; return 0;
}
static int admit(const sh4der_kos_image_t *image, const float rect[4], float z, const void *out) {
    if(!image || !image->surface || !rect || !out || ((uintptr_t)out&31u)) { errno=EINVAL;return -1; }
    for(unsigned i=0;i<4;++i) if(!isfinite(rect[i])) { errno=EDOM;return -1; }
    if(!isfinite(z) || z<=0 || rect[2]<=0 || rect[3]<=0 ||
       !isfinite(rect[0]+rect[2]) || !isfinite(rect[1]+rect[3])) { errno=EDOM;return -1; }
    return 0;
}
static int sources(const sh4der_warp_frame_t *f) {
    if(!f || f->n>SH4DER_WARP_SOURCES) { errno=EINVAL;return -1; }
    for(unsigned i=0;i<f->n;++i) {
        const sh4der_warp_src_t *s=&f->src[i];
        if(s->kind>SH4DER_WARP_WOBBLE || !isfinite(s->age) || s->age<0 ||
           !isfinite(s->strength) || s->strength<0 || s->strength>1) { errno=EDOM;return -1; }
        if(s->kind==SH4DER_WARP_RING) {
            if(!isfinite(s->cx) || !isfinite(s->cy)) { errno=EDOM;return -1; }
            float dx=fabsf(s->cx)+320,dy=fabsf(s->cy)+240;
            if(!isfinite(dx*dx+dy*dy) || !isfinite(s->age*SH4DER_WARP_RING_SPEED)) { errno=ERANGE;return -1; }
        } else {
            float phase;
            if(s->kind==SH4DER_WARP_HAZE) {
                if(!isfinite(s->x0) || !isfinite(s->y0) || !isfinite(s->x1) ||
                   !isfinite(s->y1) || s->x1<=s->x0 || s->y1<=s->y0) { errno=EDOM;return -1; }
                phase=(240+SH4DER_WARP_HAZE_RISE*s->age)*(1.0f/SH4DER_WARP_HAZE_PERIOD);
            } else phase=240.0f/SH4DER_WARP_WOBBLE_PERIOD+s->age*(1.0f/SH4DER_WARP_WOBBLE_TURN);
            /* shz_fractf reaches signed int32 truncation; keep a wide margin. */
            if(!isfinite(phase) || phase>1073741824.0f) { errno=ERANGE;return -1; }
        }
    }
    return 0;
}
static void row(const sh4der_kos_warp_grid_t *g, const sh4der_warp_frame_t *f,
    unsigned j, sh4der_warp_vtx_t *out) {
    float wave[SH4DER_WARP_SOURCES]={0};
    for(unsigned k=0;k<f->n;++k) wave[k]=sh4der_warp_wave_(&f->src[k],g->y[j]);
    for(unsigned i=0;i<=g->nx;++i) {
        float p[2]; sh4der_warp_point(f,wave,g->x[i],g->y[j],p);
        out[i]=(sh4der_warp_vtx_t){g->x[i],g->y[j],(p[0]-.5f)*(1.0f/319),(p[1]-.5f)*(1.0f/239)};
    }
}
int sh4der_kos_warp_mesh(const sh4der_kos_image_t *image,
    const sh4der_kos_warp_grid_t *g, const sh4der_warp_frame_t *f,
    const float rect[4], float z, sh4der_kos_warp_scratch_t *scratch,
    pvr_vertex_t *out, size_t capacity) {
    if(admit(image,rect,z,out)<0 || sources(f)<0) return -1;
    if(!g || !scratch) { errno=EINVAL;return -1; }
    size_t count=2*(g->nx+1)*g->ny;
    if(capacity<count) { errno=ENOSPC;return -1; }
    row(g,f,0,scratch->rows[0]);
    unsigned n=0;
    for(unsigned j=0;j<g->ny;++j) {
        row(g,f,j+1,scratch->rows[(j+1)&1]);
        for(unsigned i=0;i<=g->nx;++i) for(unsigned k=0;k<2;++k) {
            const sh4der_warp_vtx_t *p=&scratch->rows[(j+k)&1][i];
            out[n++]=(pvr_vertex_t){
                .flags=i==g->nx && k ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
                .x=rect[0]+p->x*(rect[2]*(1.0f/320)),
                .y=rect[1]+p->y*(rect[3]*(1.0f/240)),.z=z,
                .u=image->uv[0]+p->u*image->uv[2],.v=image->uv[1]+p->v*image->uv[3],.argb=0xffffffff
            };
        }
    }
    return (int)n;
}
int sh4der_kos_warp_blur(const sh4der_kos_image_t *image,
    const float rect[4], const float center[2], float strength, unsigned copies,
    float z, float z_step, sh4der_kos_screen_packet_t *out, size_t capacity) {
    if(admit(image,rect,z,out)<0) return -1;
    if(!center || !copies || copies>SH4DER_WARP_BLUR_COPIES) { errno=EINVAL;return -1; }
    if(!isfinite(center[0]) || !isfinite(center[1]) || center[0]<0 || center[0]>1 ||
       center[1]<0 || center[1]>1 || !isfinite(strength) || strength<0 || strength>1 ||
       !isfinite(z_step) || z_step<=0) { errno=EDOM;return -1; }
    if(capacity<copies) { errno=ENOSPC;return -1; }
    sh4der_kos_screen_packet_t next[SH4DER_WARP_BLUR_COPIES];
    float last=0;
    for(unsigned i=0;i<copies;++i) {
        float scale=sh4der_warp_blur_scale(i,copies,strength), depth=z+i*z_step;
        float r[4]={rect[0]+rect[2]*center[0]*(1-scale),rect[1]+rect[3]*center[1]*(1-scale),rect[2]*scale,rect[3]*scale};
        if(!isfinite(depth) || depth<=last) { errno=ERANGE;return -1; }
        last=depth;
        unsigned w=sh4der_warp_blur_weight(i,copies);
        if(sh4der_kos_image_quad(image,i?SH4DER_KOS_IMAGE_ADD:SH4DER_KOS_IMAGE_COPY,r,depth,
            0xff000000u|(w<<16)|(w<<8)|w,&next[i])<0) return -1;
    }
    memcpy(out,next,copies*sizeof(*out)); return (int)copies;
}
int sh4der_kos_warp_pixel(const sh4der_kos_image_t *image,
    const float rect[4], float z, pvr_vertex_t *out, size_t capacity) {
    if(admit(image,rect,z,out)<0) return -1;
    if(capacity<SH4DER_KOS_WARP_PIXEL_VERTS) { errno=ENOSPC;return -1; }
    unsigned n=0;
    for(unsigned y=0;y<15;++y) for(unsigned x=0;x<20;++x) {
        float u=image->uv[0]+(x*16+8)*(1.0f/319)*image->uv[2];
        float v=image->uv[1]+(y*16+8)*(1.0f/239)*image->uv[3];
        for(unsigned k=0;k<4;++k) out[n++]=(pvr_vertex_t){
            .flags=k==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX,
            .x=rect[0]+(x+(k&1))*(rect[2]*(1.0f/20)),
            .y=rect[1]+(y+((k>>1)&1))*(rect[3]*(1.0f/15)),.z=z,
            .u=u,.v=v,.argb=0xffffffff
        };
    }
    return (int)n;
}
int sh4der_kos_warp_dither(const sh4der_kos_image_t *tile,
    const float rect[4], float z, sh4der_kos_screen_packet_t *out) {
    if(sh4der_kos_image_quad(tile,SH4DER_KOS_IMAGE_MULTIPLY,rect,z,0xffffffff,out)<0) return -1;
    for(unsigned k=0;k<4;++k) { out->vertices[k].u=(k&1)?40:0;out->vertices[k].v=(k&2)?30:0; }
    return 0;
}
