/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <sh4der_kos_torch.h>
#include <sh4der_kos_frame.h>
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static sh4der_kos_torch_plan_t plan;
static sh4der_kos_torch_fog_plan_t fog;
static sh4der_torch_vert_t scratch[SH4DER_TORCH_VERTS];
static pvr_vertex_t mesh[SH4DER_TORCH_VERTS];
static sh4der_kos_overlay_t overlay;
static sh4der_kos_frame_binding_t frame;
static uint16_t samples[4800];
static float base[129],bent[129];
static int quad(const pvr_poly_hdr_t *header,float x0,float x1,float z,uint32_t rgb) {
    pvr_vertex_t v[4]={
        {.flags=PVR_CMD_VERTEX,.x=x0,.y=0,.z=z,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=x1,.y=0,.z=z,.argb=rgb},
        {.flags=PVR_CMD_VERTEX,.x=x0,.y=64,.z=z,.argb=rgb},
        {.flags=PVR_CMD_VERTEX_EOL,.x=x1,.y=64,.z=z,.argb=rgb}
    };
    if(pvr_prim(header,sizeof(*header))<0)return -1;
    return pvr_prim(v,sizeof(v));
}
int main(void) {
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},128*1024,0,0,0,0,0};
    pvr_txr_surface_t target={0};pvr_render_ticket_t ticket;sh4der_kos_frame_id_t result;
    pvr_poly_cxt_t c;pvr_poly_hdr_t header,fog_header;
    const sh4der_kos_torch_shape_t shape={48,32,20,20,.96f,.88f,.2f,3};
    unsigned baseline=0;int initialized=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);
    CHECK(sh4der_kos_overlay_prepare(&overlay)==0);
    CHECK(sh4der_kos_torch_prepare(&plan,&shape,scratch,SH4DER_TORCH_VERTS)==0);
    CHECK(sh4der_kos_torch_fog_prepare(&fog,base,129,8)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&header,&c);
    c.gen.fog_type=PVR_FOG_TABLE;pvr_poly_compile(&fog_header,&c);
    for(unsigned pass=0;pass<5;++pass) {
        int n=0;
        if(pass<3){n=sh4der_kos_torch_mesh(&plan,pass?1:0,pass==1?1:0,10,11,mesh,SH4DER_TORCH_VERTS);CHECK(n>=0);}
        CHECK(pvr_wait_ready()==0);
        if(pass>=3) {
            CHECK(sh4der_kos_torch_fog(&fog,pass==4?1:0,bent,129)==0);
            pvr_fog_far_depth(fog.far_depth);pvr_fog_table_color(1,0,0,0);pvr_fog_table_custom(bent);
        }
        CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);CHECK(pvr_list_begin(PVR_LIST_OP_POLY)==0);
        if(pass<3)CHECK(quad(&header,0,96,1,0xff606060)==0);
        else {CHECK(quad(&fog_header,0,48,1,0xffffffff)==0);CHECK(quad(&fog_header,48,96,.125f,0xffffffff)==0);}
        CHECK(pvr_list_finish()==0);
        if(n) {
            CHECK(pvr_list_begin(PVR_LIST_TR_POLY)==0);
            CHECK(pvr_prim(&overlay.headers[SH4DER_KOS_OVERLAY_MULTIPLY],sizeof(pvr_poly_hdr_t))==0);
            CHECK(pvr_prim(mesh,SH4DER_KOS_TORCH_CARVE_VERTS*sizeof(*mesh))==0);
            CHECK(pvr_prim(&overlay.headers[SH4DER_KOS_OVERLAY_BOOST],sizeof(pvr_poly_hdr_t))==0);
            CHECK(pvr_prim(mesh+SH4DER_KOS_TORCH_CARVE_VERTS,SH4DER_KOS_TORCH_BOOST_VERTS*sizeof(*mesh))==0);
            CHECK(pvr_list_finish()==0);
        }
        CHECK(pvr_scene_finish_tracked(&ticket)==0);CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&ticket,1,samples,NULL,4800,&result)==0);
        unsigned center=samples[30*80+40],corner=samples[0],left=samples[30*80+20],right=samples[30*80+60];
        printf("TORCH pass=%u vertices=%d center=%04x corner=%04x left=%04x right=%04x\n",pass,n,center,corner,left,right);
        if(pass==0){CHECK(n==0);baseline=center;CHECK(corner==baseline);}
        if(pass==1){CHECK(center>baseline);CHECK(corner<baseline);}
        if(pass==2){CHECK(center>baseline);CHECK(corner==baseline);}
        if(pass==3)CHECK(left==0xffff && right==0xffff);
        if(pass==4)CHECK(left==0xffff && right==0);
    }
    pvr_txr_surface_release(&target);pvr_shutdown();puts("RESULT: PASS (SH4DER torch and fog)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
