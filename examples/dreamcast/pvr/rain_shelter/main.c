/* Rain hidden by a prepared world-space shelter keeps its simulation slot. */
#include <kos.h>
#include <dc/pvr/pvr_fog.h>
#include <dc/pvr/pvr_misc.h>
#include <dc/pvr/pvr_txr.h>
#include <stdio.h>
#include <string.h>
#include "rain.h"

enum { WIDTH=64, HEIGHT=64, GROUPS=8 };
static const matrix_t projection={{16,0,0,0},{0,16,0,0},{32,32,0,1},{0,0,1,0}};
static const matrix_t display={{96,0,0,0},{0,96,0,0},{320,240,0,1},{0,0,1,0}};
static pvr_poly_hdr_t rain_headers[2], roof_header;
static uint16_t pixels[WIDTH*HEIGHT];
static uint8_t mask[2][WIDTH*HEIGHT];
static alignas(32) pvr_vertex_t vertices[6*RAIN_DROPS*GROUPS];

#define CHECK(expr) do { if(!(expr)) { \
    printf("SHELTERED RAIN FAIL line %d: %s\n",__LINE__,#expr); fflush(stdout); return -1; \
} } while(0)

static int render(pvr_particle_t *drops, size_t count, bool fog, pvr_txr_surface_t *target,
                  pvr_particle_emit_result_t *result) {
    pvr_particle_stream_t stream={drops,count,sizeof(*drops)};
    pvr_particle_billboard_desc_t desc={.45f,.9f,{{1,0,0,0},{0,1,0,0}},
                                        target ? &projection : &display};
    CHECK(pvr_particle_compile_billboards(vertices,6*RAIN_DROPS*GROUPS,&stream,&desc,result)==0);
    CHECK(pvr_wait_ready()==0);
    if(target) {
        for(unsigned i=0; i<WIDTH*HEIGHT; ++i) pixels[i]=0xf81f;
        CHECK(pvr_txr_surface_upload(target,pixels,sizeof(pixels),PVR_TXR_TRANSFER_CPU)==0);
        CHECK(pvr_txr_surface_begin_render(target,WIDTH,HEIGHT)==0);
    }
    else
        pvr_scene_begin();
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
    if(!target) {
        /* The eave marks the top of the shelter. The area below it stays open. */
        static const pvr_vertex_t eave[]={
            {.flags=PVR_CMD_VERTEX,.x=272,.y=144,.z=.5f,.argb=0xff909090},
            {.flags=PVR_CMD_VERTEX,.x=272,.y=138,.z=.5f,.argb=0xff909090},
            {.flags=PVR_CMD_VERTEX,.x=356,.y=144,.z=.5f,.argb=0xff909090},
            {.flags=PVR_CMD_VERTEX_EOL,.x=356,.y=138,.z=.5f,.argb=0xff909090}};
        CHECK(pvr_prim(&roof_header,sizeof(roof_header))==0);
        CHECK(pvr_prim(eave,sizeof(eave))==0);
    }
    CHECK(pvr_list_finish()==0);
    CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
    CHECK(pvr_prim(&rain_headers[fog],sizeof(rain_headers[0]))==0);
    pvr_geometry_sink_t sink;
    CHECK(pvr_geometry_sink_init_current(&sink)==0);
    CHECK(pvr_geometry_sink_emit(&sink,vertices,result->produced_vertices)==0);
    CHECK(pvr_list_finish()==0);
    pvr_render_ticket_t ticket;
    CHECK(pvr_scene_finish_tracked(&ticket)==0);
    CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,2000)==0);
    if(target)
        CHECK(pvr_txr_surface_readback(target,pixels,sizeof(pixels))==0);
    return 0;
}

static unsigned sample(const pvr_particle_t *drop) {
    unsigned x=(unsigned)(32+16*drop->position.x/drop->position.z);
    unsigned y=(unsigned)(32+16*drop->position.y/drop->position.z);
    return pixels[y*WIDTH+x];
}

/* Fog changes the drop color. Coverage at each pixel must remain identical. */
static int check_mask(unsigned reference, bool fog) {
    for(unsigned i=0; i<WIDTH*HEIGHT; ++i) {
        unsigned pixel=pixels[i];
        if(!fog) {
            CHECK(pixel==0 || pixel==0xffff);
            mask[reference][i] = pixel!=0;
        }
        else if(mask[reference][i]) {
            CHECK((pixel>>11)>3 && (pixel>>11)<28 && (pixel&31u)>=29);
        }
        else
            CHECK(pixel==0);
    }
    return 0;
}

static int check_pose(pvr_particle_t drops[RAIN_DROPS], bool covered, bool expired,
                      pvr_txr_surface_t *target) {
    for(unsigned fog=0; fog<2; ++fog) {
        pvr_particle_emit_result_t result;
        CHECK(render(drops,RAIN_DROPS,fog,target,&result)==0);
        CHECK(check_mask(0,fog)==0);
        unsigned moving=sample(&drops[0]), control=sample(&drops[2]);
        printf("RAIN hidden=%u expired=%u fog=%u moving=%04x open=%04x emitted=%u\n",
               covered,expired,fog,moving,control,(unsigned)result.emitted_items);
        CHECK(pixels[0]==0 && sample(&drops[1])==0);
        CHECK(result.emitted_items==(covered ? 1u : 2u));
        CHECK(!!(drops[1].flags & PVR_PARTICLE_ACTIVE) != expired);
        if(fog)
            CHECK((control>>11)>3 && (control>>11)<28 && (control&31u)>=29);
        else
            CHECK(control==0xffff);
        CHECK((moving!=0)==!covered);
        if(covered) {
            /* Same state, no shelter: fogged and clear probes must both appear. */
            pvr_particle_t uncovered[RAIN_DROPS];
            memcpy(uncovered,drops,sizeof(uncovered));
            rain_visibility(uncovered,NULL);
            CHECK(render(uncovered,RAIN_DROPS,fog,target,&result)==0);
            CHECK(check_mask(1,fog)==0);
            CHECK(sample(&uncovered[2])==control);
            CHECK(sample(&uncovered[0])!=0 && result.emitted_items==(expired ? 2u : 3u));
            CHECK((sample(&uncovered[1])==0)==expired);
        }
    }
    return 0;
}

int main(void) {
    vid_set_mode(DM_640x480,PM_RGB565);
    pvr_init_params_t init={.opb_sizes={PVR_BINSIZE_16,0,PVR_BINSIZE_16},.vertex_buf_size=256*1024};
    CHECK(pvr_init(&init)==0);
    pvr_set_bg_color(0,0,0);
    /* Set fog once for this scene. Individual materials select it. */
    float table[129];
    for(unsigned i=0; i<129; ++i) table[i]=.70f-.35f*(float)i/128;
    pvr_fog_table_color(1,0,0,1);
    pvr_fog_far_depth(4);
    pvr_fog_table_custom(table);
    pvr_poly_cxt_t ctx;
    pvr_poly_cxt_col(&ctx,PVR_LIST_OP_POLY);
    ctx.gen.culling=PVR_CULLING_NONE;
    ctx.gen.fog_type=PVR_FOG_DISABLE;
    pvr_poly_compile(&roof_header,&ctx);
    pvr_poly_cxt_col(&ctx,PVR_LIST_TR_POLY);
    ctx.gen.culling=PVR_CULLING_NONE;
    ctx.gen.fog_type=PVR_FOG_DISABLE;
    pvr_poly_compile(&rain_headers[0],&ctx);
    ctx.gen.fog_type=PVR_FOG_TABLE;
    pvr_poly_compile(&rain_headers[1],&ctx);

    pvr_txr_surface_t target={0};
    CHECK(pvr_txr_surface_alloc(&target,WIDTH,HEIGHT,PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR,false)==0);
    pvr_particle_t drops[RAIN_DROPS];
    rain_start(drops);
    rain_visibility(drops,&roof);
    CHECK(check_pose(drops,false,false,&target)==0);
    for(unsigned i=0; i<5; ++i) {
        CHECK(rain_step(drops,&roof,gusts[i])==0);
        if(i==1 || i==2 || i==3)
            CHECK(check_pose(drops,i!=3,true,&target)==0);
    }
    pvr_txr_surface_release(&target);

    /* Each pass seeds a new shower. Expired slots stay dead within a pass. */
    pvr_particle_t shower[GROUPS*RAIN_DROPS];
    bool reported=false;
    for(;;) {
        for(unsigned g=0; g<GROUPS; ++g) {
            pvr_particle_t *group=shower+g*RAIN_DROPS;
            rain_start(group);
            for(unsigned i=0; i<RAIN_DROPS; ++i) {
                group[i].position.x-=.35f*g;
                group[i].position.y-=.4f*g;
            }
            rain_visibility(group,&roof);
        }
        for(unsigned frame=0; frame<180; ++frame) {
            pvr_particle_emit_result_t result;
            rain_tick_t tick={1.0f/60, gusts[(frame/24)%5].wind_x};
            for(unsigned g=0; g<GROUPS; ++g)
                CHECK(rain_step(shower+g*RAIN_DROPS,&roof,tick)==0);
            CHECK(render(shower,GROUPS*RAIN_DROPS,true,NULL,&result)==0);
            if(!reported) {
                puts("SHELTERED RAIN RTT PASS"); fflush(stdout);
                reported=true;
            }
        }
    }
}
