/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_visor.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static sh4der_kos_visor_plan_t plan;
static sh4der_kos_overlay_t overlay;
static sh4der_kos_image_t images[3];
static sh4der_kos_frame_binding_t frame;
static alignas(32) uint16_t thermal[8192];
static uint16_t grid[4800],samples[4800];
static sh4der_kos_screen_packet_t packets[8];
static pvr_vertex_t mesh[408];
static int background(const pvr_poly_hdr_t *header) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=0xff808080},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=0,.z=1,.argb=0xff808080},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=64,.z=1,.argb=0xff808080},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=64,.z=1,.argb=0xff808080}
    };
    if(pvr_prim(header,sizeof(*header))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t textures[3]={{0}},target={0};
    pvr_render_ticket_t producer,reader={0};sh4der_kos_frame_id_t source,result;
    pvr_poly_cxt_t c;pvr_poly_hdr_t header;
    const float rect[4]={0,0,96,64};unsigned w[3]={128,32,8},h[3]={64,32,8};
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    for(unsigned i=0;i<3;++i) {
        CHECK(pvr_txr_surface_alloc(&textures[i],w[i],h[i],PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
        CHECK(sh4der_kos_image_bind(&images[i],&textures[i],i?w[i]:80,i?h[i]:60,i==2?PVR_FILTER_NEAREST:PVR_FILTER_BILINEAR,false,i==0)==0);
    }
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);CHECK(sh4der_kos_overlay_prepare(&overlay)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(background(&header)==0);CHECK(pvr_list_finish()==0);
    CHECK(pvr_scene_finish_tracked(&producer)==0);CHECK(pvr_render_ticket_wait(&producer,PVR_RENDER_STAGE_COMPLETE,1000)==0);
    CHECK(sh4der_kos_frame_read(&frame,&producer,1,grid,NULL,4800,&source)==0);
    for(unsigned pass=0;pass<5;++pass) {
        int mode=pass<4?(int)pass:SH4DER_VISOR_NIGHT;float amount=pass==4?0:1;
        CHECK(sh4der_kos_visor_prepare(&plan,mode)==0);
        CHECK(sh4der_kos_visor_thermal(&plan,grid,4800,thermal,8192)==0);
        CHECK(sh4der_kos_image_upload(&images[0],thermal,sizeof(thermal),pass?&reader:NULL,PVR_TXR_TRANSFER_DMA)==0);
        CHECK(sh4der_kos_image_upload(&images[1],plan.grain,sizeof(plan.grain),pass?&reader:NULL,PVR_TXR_TRANSFER_SQ)==0);
        CHECK(sh4der_kos_image_upload(&images[2],plan.scan,sizeof(plan.scan),pass?&reader:NULL,PVR_TXR_TRANSFER_CPU)==0);
        int n=sh4der_kos_visor_packets(&plan,&overlay,&images[0],&images[1],&images[2],amount,pass+5,rect,10,1,packets,8);CHECK(n>=0);
        int vn=sh4der_kos_visor_vignette(&plan,amount,rect,30,mesh,408);CHECK(vn>=0);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(background(&header)==0);CHECK(pvr_list_finish()==0);
        if(n || vn) {
            CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
            for(int i=0;i<n;++i)CHECK(pvr_prim(&packets[i],sizeof(packets[i]))==0);
            if(vn){CHECK(pvr_prim(&overlay.headers[SH4DER_KOS_OVERLAY_ALPHA],sizeof(pvr_poly_hdr_t))==0);CHECK(pvr_prim(mesh,vn*sizeof(*mesh))==0);}
            CHECK(pvr_list_finish()==0);
        }
        CHECK(pvr_scene_finish_tracked(&reader)==0);CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&reader,1,samples,NULL,4800,&result)==0);
        unsigned pixel=samples[30*80+40],r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
        printf("VISOR pass=%u mode=%d layers=%d vertices=%d pixel=%04x\n",pass,mode,n,vn,pixel);
        if(pass==0 || pass==4){CHECK(n==0 && vn==0);for(unsigned i=0;i<4800;++i)CHECK(samples[i]==grid[i]);}
        else if(mode==SH4DER_VISOR_NIGHT)CHECK(g>2*r && g>2*b);
        else CHECK(r>2*b && 2*r>g);
    }
    for(unsigned i=0;i<3;++i)pvr_txr_surface_release(&textures[i]);
    pvr_txr_surface_release(&target);pvr_shutdown();puts("RESULT: PASS (SH4DER visor modes)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
