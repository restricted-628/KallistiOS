/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black.
   Original retained sample -> glow -> upload -> halo RTT regression. */
#include <kos.h>
#include <sh4der_kos_glow.h>
#include <errno.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(expr) do { if(!(expr)) { printf("FAIL line %d: %s (errno %d)\n", __LINE__, #expr, errno); goto fail; } } while(0)
static uint16_t grid[SH4DER_KOS_FRAME_SAMPLES], result[SH4DER_KOS_FRAME_SAMPLES], small_copy[512];
static uint8_t lut[256];
static sh4der_kos_glow_stage_t maps;
static sh4der_kos_glow_binding_t glow;
static sh4der_kos_frame_binding_t frame_binding;
static sh4der_kos_screen_packet_t packets[3];
static sh4der_kos_glow_strip_t strips[6];

static int panel(const pvr_poly_hdr_t *header, float x0, float x1, uint32_t color) {
    pvr_vertex_t v[4] = {
        {.flags=PVR_CMD_VERTEX, .x=x0, .y=0, .z=1, .argb=color},
        {.flags=PVR_CMD_VERTEX, .x=x1, .y=0, .z=1, .argb=color},
        {.flags=PVR_CMD_VERTEX, .x=x0, .y=64, .z=1, .argb=color},
        {.flags=PVR_CMD_VERTEX_EOL, .x=x1, .y=64, .z=1, .argb=color}
    };
    if(pvr_prim(header,sizeof(*header)) < 0) return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    const pvr_init_params_t params = {
        {PVR_BINSIZE_16, 0, PVR_BINSIZE_16, 0, 0}, 128 * 1024, 0, 0, 0, 0, 0
    };
    pvr_txr_surface_t target={0}, big={0}, small={0};
    pvr_render_ticket_t producer, reader={0};
    sh4der_kos_frame_id_t source, output_id;
    pvr_poly_cxt_t context;
    pvr_poly_hdr_t header;
    const float rect[4]={0,0,96,64};
    int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);
    CHECK(pvr_init(&params)==0); initialized=1;
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&big,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(pvr_txr_surface_alloc(&small,32,16,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame_binding,&target,96,64,1)==0);
    CHECK(sh4der_kos_glow_bind(&glow,&big,&small)==0);
    sh4der_glow_curve(lut,0.8f,0); /* Constant authoring data: once at load. */
    pvr_poly_cxt_col(&context,PVR_LIST_OP_POLY);
    context.gen.culling=PVR_CULLING_NONE;
    context.gen.fog_type=PVR_FOG_DISABLE;
    pvr_poly_compile(&header,&context);
    for(unsigned cycle=0;cycle<3;++cycle) {
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        CHECK(panel(&header,0,48,cycle==1 ? 0xff000000 : 0xffffffff)==0);
        CHECK(panel(&header,48,96,0xff000000)==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&producer)==0);
        CHECK(pvr_render_ticket_wait(&producer,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&producer,1,grid,NULL,4800,&source)==0);
        CHECK(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,NULL,0)==0);
        /* reader is the last use of the DESTINATION MAPS; producer is not. */
        CHECK(sh4der_kos_glow_upload(&glow,&maps,cycle ? &reader : NULL,
                                    (pvr_txr_transfer_t)cycle)==0);
        CHECK(pvr_txr_surface_readback(&small,small_copy,sizeof(small_copy))==0);
        CHECK(small_copy[0]==(cycle==1 ? 0 : 0xffff));
        CHECK(small_copy[19]==0 && small_copy[20]==0);
        int n=sh4der_kos_glow_halo_packets(&glow,rect,10,1,1,
                cycle==0 ? 0 : cycle==1 ? 1 : .5f,cycle==2 ? .25f : 0,packets,3);
        CHECK(n==(cycle==2 ? 3 : 1));
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        CHECK(panel(&header,0,96,0xff000000)==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
        for(int i=0;i<n;++i) CHECK(pvr_prim(&packets[i],sizeof(packets[i]))==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,result,NULL,4800,&output_id)==0);
        CHECK(output_id.render==reader.id && glow.source.render==producer.id);
        for(unsigned y=0;y<60;++y) {
            CHECK(result[y*80+10]==(cycle==1 ? 0 : 0xffff));
            CHECK(result[y*80+60]==0);
        }
        printf("GLOW cycle=%u transfer=%u layers=%d left=%04x right=%04x\n",
               cycle,cycle,n,result[10],result[60]);
    }
    /* Reference the already-tested halo path at the same packed weight.
       RGB565 output quantization is not the CPU's RGB565 channel average. */
    CHECK(sh4der_kos_glow_halo_packets(&glow,rect,10,1,.5f,0,0,packets,3)==1);
    CHECK(packets[0].vertices[0].argb==0xff808080);
    CHECK(pvr_wait_ready()==0);
    CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
    CHECK(panel(&header,0,96,0xff000000)==0);
    CHECK(pvr_list_finish()==0);
    CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
    CHECK(pvr_prim(&packets[0],sizeof(packets[0]))==0);
    CHECK(pvr_list_finish()==0);
    CHECK(pvr_scene_finish_tracked(&reader)==0);
    CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
    CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,result,NULL,4800,&output_id)==0);
    uint16_t half_reference=result[30*80+10];
    CHECK(half_reference!=0 && half_reference!=0xffff && result[30*80+60]==0);
    printf("GLOW half-weight halo reference=%04x\n",half_reference);
    const float anchor[2]={.5f,.5f};
    for(unsigned effect=0;effect<2;++effect) {
        int n=effect ? sh4der_kos_glow_shaft_packets(&glow,rect,10,anchor,.5f,.5f,strips,6)
                     : sh4der_kos_glow_ray_packets(&glow,rect,10,1,anchor,1,1,strips,6);
        CHECK(n==6);
        CHECK(pvr_wait_ready()==0);
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        CHECK(panel(&header,0,96,0xff000000)==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
        for(int i=0;i<n;++i) CHECK(pvr_prim(&strips[i],sizeof(strips[i]))==0);
        CHECK(pvr_list_finish()==0);
        CHECK(pvr_scene_finish_tracked(&reader)==0);
        CHECK(pvr_render_ticket_wait(&reader,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame_binding,&reader,1,result,NULL,4800,&output_id)==0);
        printf("GLOW sample effect=%u left=%04x right=%04x color=%08lx uv=%f,%f\n",effect,result[30*80+10],result[30*80+60],(unsigned long)strips[0].vertices[0].argb,(double)strips[0].vertices[0].u,(double)strips[0].vertices[0].v);
        if(effect) {
            CHECK(result[30*80+10]==half_reference && result[30*80+60]==0);
        } else {
            CHECK(result[30*80+20]>result[0] && result[30*80+20]<0xffff);
            CHECK(result[30*80+60]==0);
        }
        printf("GLOW mesh=%u center-left=%04x corner=%04x\n",effect,result[30*80+20],result[0]);
    }
    /* Both map readers and target producer are COMPLETE here. */
    pvr_txr_surface_release(&small);
    pvr_txr_surface_release(&big);
    pvr_txr_surface_release(&target);
    pvr_shutdown();
    puts("RESULT: PASS (SH4DER glow maps, halo, ray and shaft packets)");
    return 0;
fail:
    /* Do not free resources still owned by a failed/pending GPU operation. */
    if(initialized) pvr_shutdown();
    return 1;
}
