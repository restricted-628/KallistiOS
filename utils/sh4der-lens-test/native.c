/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_lens.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)) {printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}} while(0)
static alignas(32) uint16_t tile[4096];
static uint16_t samples[4800],previous[4800];
static sh4der_kos_frame_binding_t frame;
static sh4der_kos_overlay_t overlay;
static sh4der_kos_lens_grain_t grain;
static sh4der_kos_screen_packet_t packets[4];
static sh4der_lens_vtx_t scratch[SH4DER_KOS_LENS_VIG_MAX];
static pvr_vertex_t vignette[SH4DER_KOS_LENS_VIG_MAX];
static int panel(const pvr_poly_hdr_t *header,uint32_t color) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=0,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=64,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=64,.z=1,.argb=color}
    };
    if(pvr_prim(header,sizeof(*header))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t texture={0},target={0};
    pvr_render_ticket_t reader;
    sh4der_kos_frame_id_t id;
    sh4der_lens_vig_t vig;
    sh4der_lens_hit_t hits[4]={0};
    pvr_poly_cxt_t c;pvr_poly_hdr_t header;
    const float rect[4]={0,0,96,64},blue[3]={.2f,.4f,1},red[3]={1,0,0};
    int initialized=0;float mean;
    CHECK(vid_mode->pm==PM_RGB565);
    CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&texture,64,64,PVR_TXR_SURFACE_ARGB4444,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);
    CHECK(sh4der_kos_overlay_prepare(&overlay)==0);
    CHECK(sh4der_kos_lens_grain_bake(tile,4096,SH4DER_LENS_GRAIN_SEED,&mean)==0);
    CHECK(sh4der_kos_lens_grain_bind(&grain,&texture,.25f,mean)==0);
    CHECK(sh4der_kos_image_upload(&grain.image,tile,sizeof(tile),NULL,PVR_TXR_TRANSFER_SQ)==0);
    sh4der_lens_vig_default(&vig,96,64);
    int vn=sh4der_kos_lens_vignette(&vig,10,scratch,1122,vignette,1122);CHECK(vn==594);
    CHECK(sh4der_lens_hit_add(hits,4,1,0)==1);sh4der_lens_hit_tick(hits,4,.5f);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    for(unsigned effect=0;effect<7;++effect) {
        int n=0;
        if(effect==1 || effect==2) n=sh4der_kos_lens_grain_packets(&grain,&overlay,9+effect,rect,10,1,packets,4);
        else if(effect==3) n=sh4der_kos_lens_hits(&overlay,hits,4,rect,10,1,packets,4);
        else if(effect==4) n=sh4der_kos_lens_flash(&overlay,.25f,blue,true,rect,10,packets);
        else if(effect==5) n=sh4der_kos_lens_flash(&overlay,.5f,red,false,rect,10,packets);
        else if(effect==6) {CHECK(sh4der_kos_overlay_quad(&overlay,SH4DER_KOS_OVERLAY_INVERT,rect,10,0xffffffff,packets)==0);n=1;}
        CHECK(n>=0);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        CHECK(panel(&header,effect==0?0xffffffff:effect==6?0xffff0000:0xff808080)==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
        if(!effect) {
            CHECK(pvr_prim(&overlay.headers[SH4DER_KOS_OVERLAY_MULTIPLY],sizeof(pvr_poly_hdr_t))==0);
            CHECK(pvr_prim(vignette,vn*sizeof(*vignette))==0);
        } else for(int i=0;i<n;++i) CHECK(pvr_prim(&packets[i],sizeof(packets[i]))==0);
        CHECK(pvr_list_finish()==0);CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&reader,1,samples,NULL,4800,&id)==0);
        unsigned mid=samples[30*80+40],edge=samples[30*80+78];
        printf("LENS effect=%u center=%04x edge=%04x corner=%04x\n",effect,mid,edge,samples[0]);
        if(!effect) CHECK(mid==0xffff && samples[0]<mid);
        else if(effect<=2) {
            unsigned sum=0,changed=0,min=63,max=0;
            for(unsigned i=0;i<4800;++i) {
                unsigned g=(samples[i]>>5)&63;sum+=g;if(g<min)min=g;if(g>max)max=g;
                changed+=samples[i]!=previous[i];previous[i]=samples[i];
            }
            printf("GRAIN frame=%u meanG=%f range=%u..%u changed=%u\n",9+effect,(double)sum/4800,min,max,changed);
            CHECK(max>min && sum>27*4800 && sum<35*4800);
            if(effect==2) CHECK(changed>100);
        } else if(effect==3) CHECK((edge>>11)>(edge&31) && (mid>>11)==(mid&31));
        else if(effect==4) CHECK((mid&31)>(mid>>11));
        else if(effect==5) CHECK((mid>>11)>(mid&31));
        else CHECK(mid==0x07ff);
    }
    pvr_txr_surface_release(&texture);pvr_txr_surface_release(&target);pvr_shutdown();
    puts("RESULT: PASS (SH4DER lens overlays)");return 0;
fail:
    if(initialized) pvr_shutdown();
    return 1;
}
