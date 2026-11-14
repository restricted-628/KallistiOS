/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_trail.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static sh4der_kos_image_t images[2];
static sh4der_kos_trail_history_t history[2];
static sh4der_kos_frame_binding_t frame[2];
static sh4der_kos_overlay_t overlay;
static sh4der_kos_screen_packet_t packets[4];
static uint16_t samples[4800];
static int background(const pvr_poly_hdr_t *h,uint32_t rgb,unsigned w,unsigned height) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=w,.y=0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=height,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX_EOL,.x=w,.y=height,.z=1,.argb=rgb}
    };
    if(pvr_prim(h,sizeof(*h))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t surfaces[2]={{0}};pvr_render_ticket_t ticket;
    pvr_poly_cxt_t c;pvr_poly_hdr_t header;sh4der_kos_frame_id_t id;
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(sh4der_kos_overlay_prepare(&overlay)==0);
    for(unsigned i=0;i<2;++i) {
        CHECK(pvr_txr_surface_alloc(&surfaces[i],128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
        CHECK(sh4der_kos_image_bind(&images[i],&surfaces[i],96,64,PVR_FILTER_NEAREST,false,true)==0);
        CHECK(sh4der_kos_trail_bind(&history[i],&images[i],96,64,1)==0);
        CHECK(sh4der_kos_frame_bind(&frame[i],&surfaces[i],96,64,1)==0);
    }
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    for(unsigned pass=0;pass<6;++pass) {
        unsigned dst=pass&1,src=dst^1,w=96,h=64;uint64_t generation=1;
        uint32_t rgb=pass==0?0xffff0000:pass==2 || pass==5?0xff00ff00:0xff0000ff;
        int n=0;float rect[4]={0,0,96,64};
        sh4der_taa_frame_t taa={.weight=.5f,.cut=pass==4};
        sh4der_trail_frame_t trail={.weight=.5f,.copy_w=.5f/3,.copies=3,.dx={0,8,16}};
        if(pass==5) {
            w=80;h=60;generation=2;rect[2]=w;rect[3]=h;
            sh4der_kos_frame_id_t old=history[dst].id;
            CHECK(sh4der_kos_image_bind(&images[dst],&surfaces[dst],w,h,PVR_FILTER_NEAREST,false,true)==0);
            CHECK(sh4der_kos_trail_bind(&history[dst],&images[dst],w,h,2)==0);
            CHECK(sh4der_kos_frame_bind(&frame[dst],&surfaces[dst],w,h,2)==0);
            CHECK(sh4der_kos_taa_packets(&history[dst],old,history[src].address,&overlay,&taa,rect,10,1,packets,4)==-1 && errno==ESTALE);
        }
        else if(pass) {
            if(pass==3)n=sh4der_kos_trail_packets(&history[src],history[src].id,history[dst].address,&overlay,&trail,rect,10,1,packets,4);
            else n=sh4der_kos_taa_packets(&history[src],history[src].id,history[dst].address,&overlay,&taa,rect,10,1,packets,4);
            CHECK(n>=0);
        }
        /* Previous render COMPLETE covers both its output and history reader. */
        sh4der_kos_trail_invalidate(&history[dst]);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&surfaces[dst],w,h)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(background(&header,rgb,w,h)==0);CHECK(pvr_list_finish()==0);
        if(n){CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);for(int i=0;i<n;++i)CHECK(pvr_prim(&packets[i],sizeof(packets[i]))==0);CHECK(pvr_list_finish()==0);}
        CHECK(pvr_scene_finish_tracked(&ticket)==0);
        CHECK(sh4der_kos_trail_publish(&history[dst],&ticket,generation)==0);
        CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame[dst],&ticket,generation,samples,NULL,4800,&id)==0);
        unsigned pixel=samples[30*80+40],edge=samples[30*80],r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
        printf("TEMPORAL pass=%u layers=%d id=%llu generation=%llu center=%04x edge=%04x\n",pass,n,(unsigned long long)id.render,(unsigned long long)id.generation,pixel,edge);
        if(pass==0)CHECK(pixel==0xf800);
        if(pass==1)CHECK(r>=14 && r<=17 && b>=14 && b<=17 && g==0);
        if(pass==2)CHECK(r>=6 && r<=9 && b>=6 && b<=9 && g>=29 && g<=33);
        if(pass==3)CHECK(n==4 && (edge>>11)<r && ((edge>>5)&63)<g && b>15);
        if(pass==4)CHECK(n==0 && pixel==0x001f);
        if(pass==5)CHECK(n==0 && pixel==0x07e0 && id.generation==2);
    }
    for(unsigned i=0;i<2;++i)pvr_txr_surface_release(&surfaces[i]);
    pvr_shutdown();puts("RESULT: PASS (SH4DER retained temporal)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
