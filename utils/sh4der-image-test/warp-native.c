/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_warp.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do { if(!(e)) { printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno); goto fail; } } while(0)
static alignas(32) uint16_t pixels[320*256], tile_pixels[64];
static uint16_t samples[4800], baseline[4800];
static pvr_vertex_t mesh[2460];
static sh4der_kos_image_t filtered, point, tile_image;
static sh4der_kos_frame_binding_t frame_binding;
static sh4der_kos_warp_grid_t topology;
static sh4der_kos_warp_scratch_t scratch;
static sh4der_kos_screen_packet_t packets[4], dither;
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},256*1024,0,0,0,0,0};
    pvr_txr_surface_t source={0}, target={0}, tile={0};
    pvr_render_ticket_t reader={0};
    sh4der_kos_frame_id_t id;
    sh4der_warp_frame_t frame={0};
    const float rect[4]={0,0,96,64}, center[2]={.5f,.5f};
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);
    CHECK(pvr_init(&params)==0); initialized=1;
    CHECK(pvr_txr_surface_alloc(&source,320,256,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_STRIDE,false)==0);
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&tile,8,8,PVR_TXR_SURFACE_ARGB4444,PVR_TXR_SURFACE_TWIDDLED,false)==0);
    pvr_txr_set_stride(320); /* Scene owner: one shared stride for these images. */
    CHECK(sh4der_kos_image_bind(&filtered,&source,320,240,PVR_FILTER_BILINEAR,true,true)==0);
    CHECK(sh4der_kos_image_bind(&point,&source,320,240,PVR_FILTER_NEAREST,true,true)==0);
    CHECK(sh4der_kos_image_bind(&tile_image,&tile,8,8,PVR_FILTER_NEAREST,false,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame_binding,&target,96,64,1)==0);
    CHECK(sh4der_kos_warp_grid_prepare(&topology,40,30)==0);
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<320;++x) pixels[y*320+x]=(x/16)&1 ? 0x001f : 0xf800;
    sh4der_warp_dither_tile(tile_pixels);
    CHECK(sh4der_kos_image_upload(&filtered,pixels,sizeof(pixels),NULL,PVR_TXR_TRANSFER_SQ)==0);
    CHECK(sh4der_kos_image_upload(&tile_image,tile_pixels,sizeof(tile_pixels),NULL,PVR_TXR_TRANSFER_CPU)==0);
    for(unsigned effect=0;effect<7;++effect) {
        int n=0;
        if(effect<4) {
            frame.n=effect?1:0;
            frame.src[0]=(sh4der_warp_src_t){.kind=effect?effect-1:0,.cx=160,.cy=120,
                .x0=0,.y0=0,.x1=320,.y1=240,.age=.65f,.strength=1};
            n=sh4der_kos_warp_mesh(&filtered,&topology,&frame,rect,10,&scratch,mesh,2460);
            CHECK(n==2460);
        } else if(effect==4) {
            n=sh4der_kos_warp_pixel(&point,rect,10,mesh,2460); CHECK(n==1200);
        } else if(effect==5) {
            CHECK(sh4der_kos_warp_blur(&filtered,rect,center,1,4,10,1,packets,4)==4);
        } else {
            CHECK(sh4der_kos_image_quad(&filtered,SH4DER_KOS_IMAGE_COPY,rect,10,0xffffffff,packets)==0);
            CHECK(sh4der_kos_warp_dither(&tile_image,rect,20,&dither)==0);
        }
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        if(effect<5) {
            const pvr_poly_hdr_t *header=effect==4 ? &point.headers[SH4DER_KOS_IMAGE_COPY] : &filtered.headers[SH4DER_KOS_IMAGE_COPY];
            CHECK(pvr_prim(header,sizeof(*header))==0);CHECK(pvr_prim(mesh,n*sizeof(*mesh))==0);
        } else CHECK(pvr_prim(packets,sizeof(*packets))==0);
        CHECK(pvr_list_finish()==0);
        if(effect>=5) {
            CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
            if(effect==5) for(unsigned i=1;i<4;++i) CHECK(pvr_prim(&packets[i],sizeof(packets[i]))==0);
            else CHECK(pvr_prim(&dither,sizeof(dither))==0);
            CHECK(pvr_list_finish()==0);
        }
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,samples,NULL,4800,&id)==0);
        unsigned changed=0, red=0, blue=0, mixed=0, dark=0;
        for(unsigned i=0;i<4800;++i) {
            unsigned p=samples[i], r=p>>11, b=p&31;
            CHECK(((p>>5)&63)==0); /* Effects preserve absent green. */
            changed+=p!=baseline[i];red+=p==0xf800;blue+=p==0x001f;
            mixed+=r>0 && b>0;dark+=(r>0 && r<31) || (b>0 && b<31);
            if(!effect) baseline[i]=p;
        }
        printf("WARP effect=%u changed=%u red=%u blue=%u mixed=%u dark=%u\n",effect,changed,red,blue,mixed,dark);
        if(!effect) CHECK(red>100 && blue>100);
        else if(effect<4) CHECK(changed>0 && red>100 && blue>100);
        else if(effect==4) CHECK(red>1000 && blue>1000);
        else if(effect==5) CHECK(mixed>100);
        else CHECK(dark>100);
    }
    /* The last ordered reader covers source and tile consumers before free. */
    pvr_txr_surface_release(&tile);pvr_txr_surface_release(&source);pvr_txr_surface_release(&target);
    pvr_shutdown();puts("RESULT: PASS (SH4DER warp stride and dither)");return 0;
fail:
    if(initialized) pvr_shutdown();
    return 1;
}
