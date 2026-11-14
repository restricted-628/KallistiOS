/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_look.h>
#include <sh4der_kos_glow.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static sh4der_kos_look_plan_t plan;
static sh4der_kos_look_state_t eye;
static sh4der_kos_frame_history_t history;
static sh4der_kos_frame_binding_t frame;
static sh4der_kos_overlay_t overlay;
static sh4der_kos_glow_stage_t maps;
static sh4der_kos_glow_binding_t glow;
static sh4der_kos_glow_feedback_t feedback;
static sh4der_kos_screen_packet_t grade_packets[6],halo[3];
static uint16_t samples[4800],original[4800];
static uint8_t lut[256];
static int background(const pvr_poly_hdr_t *h,uint32_t rgb) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=64,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=64,.z=1,.argb=rgb}
    };
    if(pvr_prim(h,sizeof(*h))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
static int near(unsigned x,unsigned y,unsigned tolerance){return x>y?x-y<=tolerance:y-x<=tolerance;}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t target={0},big={0},small={0};pvr_render_ticket_t ticket;
    pvr_poly_cxt_t c;pvr_poly_hdr_t header;sh4der_kos_frame_id_t id;
    const float rect[4]={0,0,96,64};int initialized=0;unsigned baseline=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(sh4der_kos_overlay_prepare(&overlay)==0);CHECK(sh4der_kos_look_prepare(&plan,NULL)==0);
    CHECK(sh4der_kos_look_reset(&eye,1)==0);CHECK(sh4der_kos_frame_history_reset(&history,1)==0);
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&big,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&small,32,16,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);CHECK(sh4der_kos_glow_bind(&glow,&big,&small)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    for(unsigned pass=0;pass<6;++pass) {
        sh4der_look_grade_t grade={{1,1,1},{0,0,0},1,1};int hn=0;
        if(pass==1 || pass==5)grade.exposure=.5f;
        if(pass==2)grade.exposure=1.5f;
        if(pass==3)for(unsigned i=0;i<3;++i)grade.lift[i]=-.1f;
        if(pass==4)grade.exposure=eye.eye.gain;
        int n=sh4der_kos_grade_packets_cached(&overlay,&grade,rect,10,1,grade_packets,6);CHECK(n>=0);
        if(pass==5) {
            /* Immutable map was built from pass0, not the current exposure. */
            CHECK(sh4der_kos_glow_upload(&glow,&maps,NULL,PVR_TXR_TRANSFER_DMA)==0);
            hn=sh4der_kos_glow_halo_packets(&glow,rect,30,1,1,0,0,halo,3);CHECK(hn==1);
        }
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(background(&header,0xff282828)==0);CHECK(pvr_list_finish()==0);
        if(n || hn){CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);for(int i=0;i<n;++i)CHECK(pvr_prim(&grade_packets[i],sizeof(grade_packets[i]))==0);for(int i=0;i<hn;++i)CHECK(pvr_prim(&halo[i],sizeof(halo[i]))==0);CHECK(pvr_list_finish()==0);}
        CHECK(pvr_scene_finish_tracked(&ticket)==0);CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&ticket,1,samples,NULL,4800,&id)==0);
        sh4der_kos_frame_metadata_t meta={.id=id,.gain=grade.exposure,.gain_ln=sh4der_look_ln_(grade.exposure),.glow=pass==5?1:0};
        CHECK(sh4der_kos_frame_history_put(&history,&meta)==0);
        unsigned pixel=samples[30*80+40],r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
        printf("LOOK pass=%u grade=%d halo=%d gain=%.6f pixel=%04x\n",pass,n,hn,(double)grade.exposure,pixel);
        if(pass==0) {
            baseline=pixel;memcpy(original,samples,sizeof(original));memset(lut,255,sizeof(lut));
            CHECK(sh4der_kos_glow_prepare(&maps,samples,4800,lut,id,NULL,0)==0);
            CHECK(sh4der_kos_look_update(&plan,&eye,samples,4800,&history,id,.1f)==0);
            CHECK(eye.eye.gain>1 && eye.eye.gain<=1.5f);
        }
        if(pass==1 || pass==2) {
            float factor=grade.exposure;
            CHECK(near(r,(unsigned)((baseline>>11)*factor),1));CHECK(near(g,(unsigned)(((baseline>>5)&63)*factor),1));CHECK(near(b,(unsigned)((baseline&31)*factor),1));
        }
        if(pass==3){CHECK(r<(baseline>>11) && g<((baseline>>5)&63) && b<(baseline&31));CHECK(near(r,(baseline>>11)>3?(baseline>>11)-3:0,1) && near(g,((baseline>>5)&63)>6?((baseline>>5)&63)-6:0,1));}
        if(pass==4)CHECK(pixel>=baseline);
        if(pass==5) {
            CHECK(sh4der_kos_glow_feedback_record(&feedback,&meta,&maps,1,0,0)==0);
            CHECK(sh4der_kos_glow_feedback_remove(&feedback,id,samples,4800)==0);
            pixel=samples[30*80+40];printf("FEEDBACK original=%04x corrected=%04x\n",baseline,pixel);
            CHECK(near(pixel>>11,baseline>>11,2) && near((pixel>>5)&63,(baseline>>5)&63,2) && near(pixel&31,baseline&31,2));
            /* Exposure consumes corrected data as gain1, tied to the same ID. */
            meta.gain=1;meta.gain_ln=0;meta.glow=0;CHECK(sh4der_kos_frame_history_put(&history,&meta)==0);
            CHECK(sh4der_kos_look_update(&plan,&eye,samples,4800,&history,id,.1f)==0);
            CHECK(sh4der_kos_glow_prepare(&maps,samples,4800,lut,id,NULL,0)==0);
            CHECK(sh4der_kos_glow_feedback_remove(&feedback,id,samples,4800)==-1 && errno==ESTALE);
        }
    }
    pvr_txr_surface_release(&big);pvr_txr_surface_release(&small);pvr_txr_surface_release(&target);
    pvr_shutdown();puts("RESULT: PASS (SH4DER exposure and feedback)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
