/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_ao.h>
#include <sh4der_kos_frame.h>
#include <sh4der_kos_image.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static sh4der_kos_frame_binding_t frame;
static sh4der_kos_image_t image;
static sh4der_kos_screen_packet_t packet;
static alignas(32) uint16_t map[8192];
static uint16_t grid[4800],samples[4800];
static uint8_t luma[4800];
static int quad(const pvr_poly_hdr_t *h,float y0,float y1,uint32_t rgb) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=y0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=y0,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=y1,.z=1,.argb=rgb},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=y1,.z=1,.argb=rgb}
    };
    if(pvr_prim(h,sizeof(*h))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t target={0},texture={0};pvr_render_ticket_t producer,reader;
    pvr_poly_cxt_t c;pvr_poly_hdr_t header;sh4der_kos_frame_id_t id;
    const float rect[4]={0,0,96,64};int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&texture,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);
    CHECK(sh4der_kos_image_bind(&image,&texture,80,60,PVR_FILTER_NEAREST,false,true)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(quad(&header,0,32,0xff000000)==0);CHECK(quad(&header,32,64,0xffffffff)==0);CHECK(pvr_list_finish()==0);
    CHECK(pvr_scene_finish_tracked(&producer)==0);CHECK(pvr_render_ticket_wait(&producer,PVR_RENDER_STAGE_COMPLETE,1000)==0);
    CHECK(sh4der_kos_frame_read(&frame,&producer,1,grid,NULL,4800,&id)==0);
    for(unsigned pass=0;pass<3;++pass) {
        if(pass==2)for(unsigned i=0;i<4800;++i)grid[i]=0xffff;
        CHECK(sh4der_kos_ao_map(grid,4800,luma,4800,map,8192)==0);
        CHECK(sh4der_kos_image_upload(&image,map,sizeof(map),pass?&reader:NULL,pass==0?PVR_TXR_TRANSFER_CPU:pass==1?PVR_TXR_TRANSFER_SQ:PVR_TXR_TRANSFER_DMA)==0);
        CHECK(sh4der_kos_image_quad(&image,SH4DER_KOS_IMAGE_MULTIPLY,rect,10,0xffffffff,&packet)==0);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(quad(&header,0,64,0xffffffff)==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);CHECK(pvr_prim(&packet,sizeof(packet))==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&reader)==0);CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&reader,1,samples,NULL,4800,&id)==0);
        unsigned boundary=samples[30*80+40],above=samples[20*80+40],below=samples[40*80+40];
        printf("CAVITY pass=%u map=%04x boundary=%04x above=%04x below=%04x\n",pass,map[30*128+40],boundary,above,below);
        CHECK(above==0xffff && below==0xffff);
        if(pass<2)CHECK(boundary<0xc618 && boundary>0x4208);
        else for(unsigned i=0;i<4800;++i)CHECK(samples[i]==0xffff);
    }
    pvr_txr_surface_release(&texture);pvr_txr_surface_release(&target);pvr_shutdown();puts("RESULT: PASS (SH4DER cavity)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
