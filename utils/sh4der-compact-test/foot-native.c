/* KallistiOS ##version##
   Original receiver-depth foot-shadow regression. Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_geometry.h>
#include <sh4der_kos_frame.h>
#include <sh4der_kos_image.h>
#include "cooked-foot.h"
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static uint16_t samples[4800];
static pvr_vertex_t quad[4],projected[4];
static sh4der_kos_image_t image;
static sh4der_kos_frame_binding_t frame;
static const matrix_t projection={{200,0,0,0},{0,-200,0,0},{0,0,1,-.25f},{48,32,0,1}};
static int rectangle(const pvr_poly_hdr_t *h,float x0,float x1,float z,uint32_t color) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=x0,.y=0,.z=z,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=x1,.y=0,.z=z,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=x0,.y=64,.z=z,.argb=color},
        {.flags=PVR_CMD_VERTEX_EOL,.x=x1,.y=64,.z=z,.argb=color}
    };
    if(pvr_prim(h,sizeof(*h))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t init={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t target={0},texture={0};pvr_render_ticket_t ticket;
    pvr_poly_cxt_t c;pvr_poly_hdr_t opaque,shadow_header;sh4der_kos_frame_id_t id;
    sh4dow_foot_params_t params;sh4dow_foot_t foot={.away={.5f,0,-1}};
    const float plane[3]={0,0,0},up[3]={0,0,1},forward[3]={1,0,0};
    pvr_geometry_sink_t sink;pvr_geometry_stream_t stream={quad,4,sizeof(*quad)};
    unsigned first_dark=0;int initialized=0;
    sh4dow_foot_params_default(&params);params.width_min=.02f;params.width_max=.08f;params.lift=.012f;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&init)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&texture,64,64,PVR_TXR_SURFACE_ARGB4444,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);
    CHECK(sh4der_kos_image_bind(&image,&texture,64,64,PVR_FILTER_BILINEAR,false,true)==0);
    CHECK(sh4der_kos_image_upload(&image,cooked_foot,sizeof(cooked_foot),NULL,PVR_TXR_TRANSFER_DMA)==0);
    CHECK(pvr_geometry_sink_init_current(&sink)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&opaque,&c);
    pvr_ptr_t address;CHECK(pvr_txr_surface_get_texture_address(&texture,&address)==0);
    pvr_poly_cxt_txr(&c,PVR_LIST_TR_POLY,PVR_TXRFMT_ARGB4444|PVR_TXRFMT_NONTWIDDLED,64,64,address,PVR_FILTER_BILINEAR);
    c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;
    c.depth.comparison=PVR_DEPTHCMP_GEQUAL;c.depth.write=PVR_DEPTHWRITE_DISABLE;
    c.txr.env=PVR_TXRENV_MODULATEALPHA;c.txr.uv_clamp=PVR_UVCLAMP_UV;
    c.blend.src=PVR_BLEND_SRCALPHA;c.blend.dst=PVR_BLEND_INVSRCALPHA;pvr_poly_compile(&shadow_header,&c);
    for(unsigned pass=0;pass<4;++pass) {
        float centre[3]={-.08f,0,pass==2?params.fade_height+.1f:.02f};
        if(pass==3)centre[1]=.07f; /* New posed foot; refresh receiver each draw. */
        sh4dow_foot_place(&foot,&params,centre,.07f,forward,up);
        int n=sh4der_kos_foot_vertices(&foot,&params,plane,up,up,forward,230,quad,4);CHECK(n>=0);
        if(n)CHECK(pvr_geometry_project(projected,4,&stream,&projection,NULL)==0);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(rectangle(&opaque,0,96,1,0xffffffff)==0);
        if(pass==1)CHECK(rectangle(&opaque,48,96,2,0xffff0000)==0);
        CHECK(pvr_list_finish()==0);
        if(n){CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);CHECK(pvr_prim(&shadow_header,sizeof(shadow_header))==0);CHECK(pvr_geometry_sink_emit(&sink,projected,4)==0);CHECK(pvr_list_finish()==0);}
        CHECK(pvr_scene_finish_tracked(&ticket)==0);CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&ticket,1,samples,NULL,4800,&id)==0);
        unsigned dark=0,sumy=0;for(unsigned y=0;y<60;++y)for(unsigned x=0;x<80;++x){unsigned p=samples[y*80+x];if(pass==1 && x>=40)CHECK(p==0xf800);else if(p!=0xffff){++dark;sumy+=y;}}
        printf("FOOT pass=%u vertices=%d dark=%u mean_y=%.3f\n",pass,n,dark,(double)(dark?(float)sumy/dark:0));
        CHECK(samples[0]==0xffff);
        if(pass==0){CHECK(n==4 && dark>100);first_dark=dark;}
        if(pass==1)CHECK(n==4 && dark>0 && dark<first_dark);
        if(pass==2)CHECK(n==0 && dark==0);
        if(pass==3)CHECK(n==4 && dark>100 && (float)sumy/dark<22);
    }
    /* Final ticket retires the alpha-mask reader, not only the RTT producer. */
    pvr_txr_surface_release(&texture);pvr_txr_surface_release(&target);pvr_shutdown();puts("RESULT: PASS (SH4DOW textured feet)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
