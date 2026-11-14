/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_ca.h>
#include <sh4der_kos_shafts.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do { if(!(e)) { printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno); goto fail; } } while(0)
static alignas(32) uint16_t pixels[320*256];
static uint16_t samples[4800], baseline[4800], grid[4800];
static sh4der_kos_image_t image,shade_image;
static sh4der_kos_ca_plan_t ca;
static sh4der_kos_ca_packet_t channels[3];
static sh4der_kos_shafts_plan_t shafts;
static sh4der_kos_shafts_stage_t stage;
static sh4der_kos_frame_binding_t frame_binding;
static sh4der_kos_screen_packet_t quad;
static pvr_vertex_t mesh[SH4DER_KOS_SHAFTS_VERTS];
static int panel(const pvr_poly_hdr_t *header,uint32_t color) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=0,.y=0,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=96,.y=0,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX,.x=0,.y=64,.z=1,.argb=color},
        {.flags=PVR_CMD_VERTEX_EOL,.x=96,.y=64,.z=1,.argb=color}
    };
    if(pvr_prim(header,sizeof(*header))<0) return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t source={0},target={0},shade={0};
    pvr_render_ticket_t reader={0},producer;
    sh4der_kos_frame_id_t id,output_id;
    const float rect[4]={0,0,96,64},sun[2]={.5f,.5f};
    pvr_poly_cxt_t c;
    pvr_poly_hdr_t header;
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);
    CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&source,320,256,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_STRIDE,false)==0);
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&shade,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    pvr_txr_set_stride(320);
    CHECK(sh4der_kos_image_bind(&image,&source,320,240,PVR_FILTER_BILINEAR,true,true)==0);
    CHECK(sh4der_kos_image_bind(&shade_image,&shade,80,60,PVR_FILTER_BILINEAR,true,true)==0);
    CHECK(sh4der_kos_frame_bind(&frame_binding,&target,96,64,1)==0);
    CHECK(sh4der_kos_ca_prepare(&ca,rect)==0);
    CHECK(sh4der_kos_shafts_prepare(&shafts,sun)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<320;++x) pixels[y*320+x]=(x/16)&1?0:0xffff;
    CHECK(sh4der_kos_image_upload(&image,pixels,sizeof(pixels),NULL,PVR_TXR_TRANSFER_SQ)==0);
    for(unsigned pass=0;pass<3;++pass) {
        if(!pass) CHECK(sh4der_kos_image_quad(&image,SH4DER_KOS_IMAGE_COPY,rect,10,0xffffffff,&quad)==0);
        else CHECK(sh4der_kos_ca_packets(&ca,&image,pass==1?0:1,10,1,channels,3)==3);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        if(!pass) CHECK(pvr_prim(&quad,sizeof(quad))==0);
        else CHECK(pvr_prim(&channels[0],sizeof(channels[0]))==0);
        CHECK(pvr_list_finish()==0);
        if(pass) {
            CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
            CHECK(pvr_prim(&channels[1],sizeof(channels[1]))==0);
            CHECK(pvr_prim(&channels[2],sizeof(channels[2]))==0);
            CHECK(pvr_list_finish()==0);
        }
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,samples,NULL,4800,&output_id)==0);
        unsigned changed=0,fringes=0;
        for(unsigned i=0;i<4800;++i) {
            if(!pass) baseline[i]=samples[i];
            else changed+=baseline[i]!=samples[i];
            fringes+=(samples[i]>>11)!=(samples[i]&31);
        }
        printf("CA pass=%u changed=%u fringes=%u\n",pass,changed,fringes);
        if(pass==1) CHECK(changed==0);
        if(pass==2) CHECK(changed>100 && fringes>100);
    }
    uint16_t center_first=0;
    for(unsigned pass=0;pass<3;++pass) {
        /* Fresh pre-effect source; never feed the shaded result back. */
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(panel(&header,0xff000000)==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&producer)==0);CHECK(pvr_render_ticket_wait(&producer,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&producer,1,grid,NULL,4800,&id)==0);
        CHECK(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,2,pass==2?-1:1,id,pass==1?&stage:NULL)==0);
        CHECK(sh4der_kos_image_upload(&shade_image,stage.pixels,sizeof(stage.pixels),pass?&reader:NULL,PVR_TXR_TRANSFER_DMA)==0);
        CHECK(sh4der_kos_shafts_mesh(&shafts,rect,10,sh4der_shafts_scale(2),mesh,108)==108);
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);CHECK(panel(&header,0xffffffff)==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
        CHECK(pvr_prim(&shade_image.headers[SH4DER_KOS_IMAGE_MULTIPLY],sizeof(pvr_poly_hdr_t))==0);
        CHECK(pvr_prim(mesh,sizeof(mesh))==0);CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&reader)==0);CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,samples,NULL,4800,&output_id)==0);
        unsigned middle=samples[30*80+40];
        printf("SHAFTS pass=%u center=%04x corner=%04x source=%llu reader=%llu\n",pass,middle,samples[0],(unsigned long long)id.render,(unsigned long long)reader.id);
        if(pass==0) {CHECK(middle>0 && middle<samples[0]);center_first=middle;}
        else if(pass==1) CHECK(middle<center_first && middle>0);
        else for(unsigned i=0;i<4800;++i) CHECK(samples[i]==0xffff);
    }
    pvr_txr_surface_release(&shade);pvr_txr_surface_release(&source);pvr_txr_surface_release(&target);
    pvr_shutdown();puts("RESULT: PASS (SH4DER CA and shadow shafts)");return 0;
fail:
    if(initialized) pvr_shutdown();
    return 1;
}
