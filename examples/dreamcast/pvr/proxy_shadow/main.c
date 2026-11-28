/* Compare a generated, finite convex shadow with a separately authored box. */
#include <kos.h>
#include <dc/pvr_frustum.h>
#include <dc/pvr/pvr_misc.h>
#include <dc/pvr/pvr_txr.h>
#include <stdio.h>
#include <string.h>
#include "proxy.h"

enum { WIDTH=64, HEIGHT=64 };
static uint16_t pixels[WIDTH*HEIGHT], reference[WIDTH*HEIGHT];
static alignas(32) pvr_modifier_vol_t packets[PROXY_MAX_TRIANGLES];
static const matrix_t projection = {{16,0,0,0},{0,16,0,0},{32,32,0,1},{0,0,1,0}};
static pvr_poly_hdr_t receiver;
static pvr_mod_hdr_t headers[2];
static pvr_frustum_t frustum;
#define CHECK(expr) do { if(!(expr)) { \
    printf("PROXY SHADOW FAIL line %d: %s\n", __LINE__, #expr); fflush(stdout); return -1; \
} } while(0)

/* Independent authored cuboid. Its caps and sides use explicit triangles. */
static void authored_box(proxy_volume_t *out, float front_z) {
    static const uint8_t triangles[12][3] = {
        {1,0,3},{3,2,1}, {4,5,7},{5,6,7},
        {0,4,3},{3,4,7}, {1,2,5},{2,6,5},
        {0,1,4},{1,5,4}, {3,7,2},{7,6,2}
    };
    for(unsigned i=0; i<8; ++i) {
        float x = (i%4 == 1 || i%4 == 2) ? 1 : -1;
        float y = (i%4 >= 2) ? 1 : -1;
        out->points[i] = shz_vec3_init(x, y, front_z + (i>=4 ? 3 : 0));
    }
    memcpy(out->triangles, triangles, sizeof(triangles));
    out->count = 12;
}

static int make_packets(const proxy_volume_t *volume, size_t *near_vertices) {
    *near_vertices = 0;
    for(size_t i = 0; i < volume->count; ++i) {
        const uint8_t *t = volume->triangles[i];
        shz_vec3_t a=volume->points[t[0]], b=volume->points[t[1]], c=volume->points[t[2]];
        alignas(32) pvr_modifier_vol_t input = {.flags=PVR_CMD_VERTEX_EOL,
            .ax=a.x,.ay=a.y,.az=a.z, .bx=b.x,.by=b.y,.bz=b.z, .cx=c.x,.cy=c.y,.cz=c.z};
        CHECK(pvr_frustum_project_modifier_warp(&packets[i], &input, &frustum) == 0);
        float depths[3] = {packets[i].az, packets[i].bz, packets[i].cz};
        for(unsigned k=0; k<3; ++k) {
            float z = volume->points[t[k]].z;
            CHECK(depths[k] > 0 && depths[k] <= 1);
            if(z < 1) {
                CHECK(depths[k] == 1);
                ++*near_vertices;
            }
        }
    }
    return 0;
}

static int render(const pvr_txr_surface_t *target, size_t count) {
    pvr_vertex_t wall[4] = {
        {.flags=PVR_CMD_VERTEX,.x=4,.y=60,.z=.4f,.argb=0xffffffff},
        {.flags=PVR_CMD_VERTEX,.x=4,.y=4,.z=.4f,.argb=0xffffffff},
        {.flags=PVR_CMD_VERTEX,.x=60,.y=60,.z=.4f,.argb=0xffffffff},
        {.flags=PVR_CMD_VERTEX_EOL,.x=60,.y=4,.z=.4f,.argb=0xffffffff}
    };
    CHECK(pvr_wait_ready() == 0);
    if(target) {
        for(size_t i=0; i<WIDTH*HEIGHT; ++i) pixels[i]=0xf81f;
        CHECK(pvr_txr_surface_upload(target, pixels, sizeof(pixels), PVR_TXR_TRANSFER_CPU)==0);
        CHECK(pvr_txr_surface_begin_render(target, WIDTH, HEIGHT)==0);
    }
    else {
        for(unsigned i=0; i<4; ++i) {
            wall[i].x = 128 + 6*wall[i].x;
            wall[i].y = 48 + 6*wall[i].y;
        }
        pvr_scene_begin();
    }
    CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
    CHECK(pvr_prim(&receiver, sizeof(receiver))==0);
    CHECK(pvr_prim(wall, sizeof(wall))==0);
    CHECK(pvr_list_finish()==0);
    CHECK(pvr_list_begin(PVR_LIST_OP_MOD)==0);
    for(size_t i=0; i<count; ++i) {
        CHECK(pvr_prim(&headers[i+1 == count], sizeof(headers[0]))==0);
        CHECK(pvr_prim(&packets[i], sizeof(packets[i]))==0);
    }
    CHECK(pvr_list_finish()==0);
    pvr_render_ticket_t ticket;
    CHECK(pvr_scene_finish_tracked(&ticket)==0);
    CHECK(pvr_render_ticket_wait(&ticket, PVR_RENDER_STAGE_COMPLETE, 2000)==0);
    if(target)
        CHECK(pvr_txr_surface_readback(target, pixels, sizeof(pixels))==0);
    return 0;
}

static size_t compare_frames(void) {
    size_t differences = 0;
    for(size_t i=0; i<WIDTH*HEIGHT; ++i) {
        if(reference[i] != pixels[i]) {
            if(!differences)
                printf("first mismatch (%u,%u): authored=%04x generated=%04x\n",
                       (unsigned)(i%WIDTH), (unsigned)(i/WIDTH), reference[i], pixels[i]);
            ++differences;
        }
    }
    return differences;
}

int main(void) {
    vid_set_mode(DM_640x480, PM_RGB565);
    pvr_init_params_t init = {.opb_sizes={PVR_BINSIZE_16,PVR_BINSIZE_16},.vertex_buf_size=256*1024};
    CHECK(pvr_init(&init)==0);
    pvr_set_bg_color(0,0,1);
    pvr_set_shadow_scale(true,.5f);
    pvr_poly_cxt_t ctx;
    pvr_poly_cxt_col(&ctx, PVR_LIST_OP_POLY);
    ctx.gen.culling=PVR_CULLING_NONE;
    ctx.gen.modifier_mode=PVR_MODIFIER_CHEAP_SHADOW;
    ctx.fmt.modifier=PVR_MODIFIER_ENABLE;
    pvr_poly_compile(&receiver,&ctx);
    pvr_mod_compile(&headers[0],PVR_LIST_OP_MOD,PVR_MODIFIER_OTHER_POLY,PVR_CULLING_NONE);
    pvr_mod_compile(&headers[1],PVR_LIST_OP_MOD,PVR_MODIFIER_INCLUDE_LAST_POLY,PVR_CULLING_NONE);
    CHECK(pvr_frustum_init(&frustum,&projection,0,0,WIDTH,HEIGHT,1,10)==0);
    pvr_txr_surface_t target={0};
    CHECK(pvr_txr_surface_alloc(&target,WIDTH,HEIGHT,PVR_TXR_SURFACE_RGB565,
                                PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(render(&target,0)==0);
    CHECK(pixels[32*WIDTH+32]==0xffff && pixels[0]==0x001f);
    for(unsigned pose=0; pose<2; ++pose) {
        float front=pose ? .875f : 1.5f;
        proxy_volume_t volume;
        size_t near_count;
        authored_box(&volume,front);
        CHECK(make_packets(&volume,&near_count)==0 && (near_count != 0)==(pose != 0));
        CHECK(render(&target,volume.count)==0);
        memcpy(reference,pixels,sizeof(pixels));
        CHECK(proxy_extrude(&volume,PROXY_MAX_TRIANGLES,front,shz_vec3_init(0,0,-1),3)==0);
        CHECK(make_packets(&volume,&near_count)==0 && (near_count != 0)==(pose != 0));
        CHECK(render(&target,volume.count)==0);
        size_t differences=compare_frames();
        unsigned center=pixels[32*WIDTH+32], outside=pixels[32*WIDTH+54];
        printf("PROXY pose=%u near_corners=%u center=%04x outside=%04x differences=%u\n",
               pose,(unsigned)near_count,center,outside,(unsigned)differences);
        CHECK(differences==0 && (center>>11)>=12 && (center>>11)<=20 &&
              outside==0xffff && pixels[0]==0x001f);
    }
    /* Reverse an oblique light. The finite shadow should cross the center. */
    int moment[2] = {0,0};
    for(unsigned i=0; i<2; ++i) {
        proxy_volume_t volume;
        size_t near_count;
        CHECK(proxy_extrude(&volume,PROXY_MAX_TRIANGLES,1.5f,
                            shz_vec3_init(i ? .6f : -.6f,0,-.8f),3)==0);
        CHECK(make_packets(&volume,&near_count)==0 && near_count==0);
        CHECK(render(&target,volume.count)==0);
        for(int y=8; y<56; ++y)
            for(int x=8; x<56; ++x)
                if((pixels[y*WIDTH+x] >> 11) < 22)
                    moment[i] += 2*x+1-WIDTH;
    }
    printf("PROXY oblique moments %d %d\n",moment[0],moment[1]);
    CHECK(moment[0]>100 && moment[1]<-100);
    pvr_txr_surface_release(&target);
    const matrix_t screen = {{96,0,0,0},{0,96,0,0},{320,240,0,1},{0,0,1,0}};
    CHECK(pvr_frustum_init(&frustum,&screen,0,0,640,480,1,10)==0);
    bool reported = false;
    for(;;) {
        proxy_volume_t volume;
        size_t near_count;
        float x = timer_ms_gettime64()%2000 < 1000 ? -.6f : .6f;
        CHECK(proxy_extrude(&volume,PROXY_MAX_TRIANGLES,1.5f,shz_vec3_init(x,0,-.8f),3)==0);
        CHECK(make_packets(&volume,&near_count)==0);
        CHECK(render(NULL,volume.count)==0);
        if(!reported) {
            puts("CLOSED PROXY SHADOW RTT PASS"); fflush(stdout);
            reported = true;
        }
    }
}
