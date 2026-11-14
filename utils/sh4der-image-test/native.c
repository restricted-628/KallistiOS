/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_focus.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do { if(!(e)) { printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno); goto fail; } } while(0)
static uint16_t grid[4800], samples[4800];
static alignas(32) uint16_t blur[8192];
static pvr_vertex_t mesh[SH4DER_FOCUS_VERTS];
static sh4der_kos_image_t image;
static sh4der_kos_frame_binding_t frame;
static sh4der_kos_screen_packet_t packet;
static int background(const pvr_poly_hdr_t *header) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=0xffff0000},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=0,.z=1,.argb=0xffff0000},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=64,.z=1,.argb=0xffff0000},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=64,.z=1,.argb=0xffff0000}
    };
    if(pvr_prim(header,sizeof(*header))<0) return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t texture={0}, target={0};
    pvr_render_ticket_t reader={0};
    sh4der_kos_frame_id_t id;
    sh4der_kos_focus_plan_t plan;
    sh4der_focus_mask_t mask;
    pvr_poly_cxt_t c;
    pvr_poly_hdr_t header;
    const float rect[4]={0,0,96,64};
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);
    CHECK(pvr_init(&params)==0); initialized=1;
    CHECK(pvr_txr_surface_alloc(&texture,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_image_bind(&image,&texture,80,60,PVR_FILTER_BILINEAR,false,true)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);
    c.gen.culling=PVR_CULLING_NONE; c.gen.fog_type=PVR_FOG_DISABLE;
    pvr_poly_compile(&header,&c);
    for(unsigned i=0;i<4800;++i) grid[i]=0xffff;
    CHECK(sh4der_kos_focus_blur(grid,4800,blur,8192)==0);
    for(unsigned shape=0;shape<3;++shape) {
        sh4der_focus_mask_default(&mask,shape);
        CHECK(sh4der_kos_focus_prepare(&plan,&mask)==0);
        CHECK(sh4der_kos_focus_mesh(&plan,rect,10,mesh,408)==408);
        CHECK(sh4der_kos_image_upload(&image,blur,sizeof(blur),shape?&reader:NULL,(pvr_txr_transfer_t)shape)==0);
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        CHECK(background(&header)==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
        CHECK(pvr_prim(&image.headers[SH4DER_KOS_IMAGE_ALPHA],sizeof(pvr_poly_hdr_t))==0);
        CHECK(pvr_prim(mesh,sizeof(mesh))==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&reader,1,samples,NULL,4800,&id)==0);
        printf("FOCUS shape=%u center=%04x corner=%04x\n",shape,samples[30*80+40],samples[0]);
        CHECK(samples[30*80+40]==0xf800 && samples[0]==0xffff);
    }
    /* All cached header modes consume the same caller-owned texture. */
    for(unsigned mode=0;mode<4;++mode) {
        uint32_t color=mode==1 ? 0x00ffffff : mode==2 ? 0xff808080 : mode==3 ? 0xff000000 : 0xffffffff;
        CHECK(sh4der_kos_image_quad(&image,(sh4der_kos_image_mode_t)mode,rect,10,color,&packet)==0);
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        if(mode) CHECK(background(&header)==0);
        else CHECK(pvr_prim(&packet,sizeof(packet))==0);
        CHECK(pvr_list_finish()==0);
        if(mode) {
            CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
            CHECK(pvr_prim(&packet,sizeof(packet))==0);
            CHECK(pvr_list_finish()==0);
        }
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&reader,1,samples,NULL,4800,&id)==0);
        unsigned pixel=samples[30*80+40];
        printf("IMAGE mode=%u pixel=%04x\n",mode,pixel);
        if(!mode) CHECK(pixel==0xffff);
        else if(mode==1) CHECK(pixel==0xf800);
        else if(mode==2) CHECK((pixel>>11)==31 && ((pixel>>5)&63)>0 && (pixel&31)>0 && pixel!=0xffff);
        else CHECK(pixel==0);
    }
    pvr_txr_surface_release(&target);
    pvr_txr_surface_release(&texture);
    pvr_shutdown();
    puts("RESULT: PASS (SH4DER image and focus)");
    return 0;
fail:
    if(initialized) pvr_shutdown();
    return 1;
}
