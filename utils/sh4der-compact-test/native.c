/* KallistiOS ##version##
   Original Compact/posed-shadow regression. Copyright (C) 2026 Joseph Black. */
#include <kos.h>
#include <dc/pvr_chunk_skin.h>
#include <dc/pvr_skin_prepared.h>
#include <sh4der_kos.h>
#include <sh4der_kos_geometry.h>
#include <sh4der_kos_frame.h>
#include <sh4der/sh4der_torch.h>
#include "../sh4der-ao-test/cooked.h"
#include <errno.h>
#include <stdio.h>
KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(e) do {if(!(e)){printf("FAIL line %d: %s errno=%d\n",__LINE__,#e,errno);goto fail;}}while(0)
static const float positions[4][3]={{0,-.035f,1},{.35f,-.035f,1},{0,.035f,1},{.35f,.035f,1}};
/* Reversed input restores4,5,6,7; repeated7 tails make degenerate triangles. */
static const uint16_t polygons[]={PVR_CHUNK_STRIP_INDEX,8,1,0x8006,5,4,6,7,7,7,0xff};
static const uint8_t weights[8]={0,0,0,0,0,5,10,15};
static const uint16_t root_bones[8]={0},child_bones[8]={1,1,1,1,1,1,1,1};
static pvr_chunk_vertex_index_entry_t index_entries[256];
static uint32_t lookups[2][256];
static alignas(32) uint8_t source_storage[2][1024];
static pvr_chunk_skin_binding_t skin_bindings[2];
static pvr_chunk_skin_source_t sources[2];
static pvr_skin_prepared_influence_t weight_storage[2][4];
static pvr_skin_prepared_influences_t weight_plans[2];
static pvr_deform_vertex_t posed[4];
static pvr_vertex_t workspace[6],bloom_vertices[SH4DER_KOS_BLOOM_VERTICES],projected[SH4DER_KOS_BLOOM_VERTICES];
static sh4der_kos_frame_binding_t frame;
static uint16_t samples[4800];
static const matrix_t identity={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
static const matrix_t projection={{200,0,0,0},{0,-400,0,0},{0,0,1,0},{13,32,0,1}};
typedef struct base_context {const uint32_t *lookup;const pvr_poly_hdr_t *header;int torch;} base_context_t;
static int base_prepare(const pvr_chunk_render_state_t *s,const pvr_chunk_vertex_attributes_t *a,
    const pvr_chunk_strip_attributes_t *strip,pvr_vertex_t *v,void *data) {
    (void)s;(void)strip;const base_context_t *ctx=data;
    /* Model and skin coverage admitted once; no repeated pose lookup checks. */
    const pvr_deform_vertex_t *p=&posed[ctx->lookup[a->index]];
    v->x=p->position.x;v->y=p->position.y;v->z=p->position.z;
    float light=.25f+.75f*p->normal.z;
    if(ctx->torch) {
        float dx=v->x-.175f,dy=v->y,dz=v->z-1;
        light=.5f+sh4der_torch_near_at(dx*dx+dy*dy+dz*dz,.5f,.5f,4);
    }
    unsigned c=(unsigned)(fminf(1,light)*255+.5f);v->argb=0xff000000u|c<<16|c<<8|c;return 0;
}
static int begin_strip(const pvr_chunk_render_state_t *s,const pvr_chunk_strip_view_t *strip,void *data) {
    (void)s;(void)strip;const sh4der_kos_effect_t *e=data;const base_context_t *ctx=e->base_data;
    return pvr_prim(ctx->header,sizeof(*ctx->header));
}
int main(void) {
    uint32_t words[15]={PVR_CHUNK_VERTEX_XYZ|(13u<<16),(4u<<16)|4u};
    pvr_chunk_model_t model={words,15,polygons,11,{.175f,0,1},1};
    pvr_chunk_model_view_t view;pvr_chunk_model_plan_t plan;pvr_chunk_skin_requirements_t req;
    pvr_chunk_skin_influence_t influences[2][4]={0};
    pvr_geometry_sink_t sink;sh4der_kos_binding_t bindings[3];
    pvr_txr_surface_t target={0};pvr_render_ticket_t ticket;sh4der_kos_frame_id_t id;
    pvr_init_params_t params={{PVR_BINSIZE_16,0,PVR_BINSIZE_16,0,0},256*1024,0,0,0,0,0};
    pvr_poly_cxt_t c;pvr_poly_hdr_t opaque,add;
    matrix_t joints[2];pvr_normal_matrix_t normals[2];pvr_skin_prepared_joint_t imported[2];
    pvr_skin_prepared_palette_t palette;pvr_skin_palette_t raw_palette={joints,normals,2};
    sh4der_blade_params_t bp;sh4der_blade_frame_t blade;
    sh4der_holo_params_t hp;sh4der_holo_frame_t holo;
    sh4der_bloom_params_t gp;sh4der_bloom_mesh_t mesh;
    sh4dow_params_t sp;sh4dow_frame_t shadow;
    const float direction[3]={0,0,1},eye[3]={.175f,0,2},edge[3]={.05f,.3f,1},core[3]={.7f,.9f,1};
    int initialized=0;unsigned base_count=0,blade_count=0;float base_center=0;
    memcpy(words+2,positions,sizeof(positions));words[14]=0xff;
    CHECK(pvr_chunk_model_open(&model,&view)==0);CHECK(pvr_chunk_model_plan_build(&view,index_entries,256,&plan)==0);
    CHECK(view.info.maximum_vertex_index==7);CHECK(pvr_chunk_skin_query(&plan,&req)==0);CHECK(req.source_bytes<=1024 && req.lookup_entries<=256);
    for(unsigned b=0;b<2;++b) {
        for(unsigned v=0;v<4;++v){influences[b][v].vertex_index=v+4;influences[b][v].joint[0]=b;influences[b][v].weight[0]=65535;}
        pvr_chunk_skin_t skin={influences[b],4,2};
        CHECK(pvr_chunk_skin_bind(&plan,&skin,lookups[b],256,&skin_bindings[b])==0);
        CHECK(pvr_chunk_skin_source_build(&skin_bindings[b],source_storage[b],1024,&sources[b])==0);
        pvr_skin_stream_t ws={sources[b].influences,4,sizeof(*sources[b].influences)};
        CHECK(pvr_skin_influences_prepare(&ws,2,weight_storage[b],4,&weight_plans[b])==0);
    }
    sh4der_kos_sidecars_t sidecars={.holo_weights=weights,.bones=root_bones,.count=8};
    CHECK(sh4der_kos_bind(&bindings[0],&view,&sidecars)==0);sidecars.ao=cooked_ao;
    CHECK(sh4der_kos_bind(&bindings[1],&view,&sidecars)==0);sidecars.ao=NULL;sidecars.bones=child_bones;
    CHECK(sh4der_kos_bind(&bindings[2],&view,&sidecars)==0);
    sh4der_blade_params_default(&bp);bp.axis[1]=1;CHECK(sh4der_blade_prepare(&blade,&bp,19,1));
    sh4der_holo_params_default(&hp);CHECK(sh4der_holo_prepare(&holo,&hp,.7f,1));
    sh4der_bloom_params_default(&gp);gp.half_width=.06f;gp.inner=.01f;gp.disc_radius=.05f;
    CHECK(sh4der_bloom_blade(&mesh,&gp,&bp,&blade,eye));
    CHECK(sh4der_kos_bloom_vertices(&mesh,edge,core,160,bloom_vertices,SH4DER_KOS_BLOOM_VERTICES)==SH4DER_KOS_BLOOM_VERTICES);
    pvr_geometry_stream_t stream={bloom_vertices,SH4DER_KOS_BLOOM_VERTICES,sizeof(*bloom_vertices)};
    CHECK(pvr_geometry_project(projected,SH4DER_KOS_BLOOM_VERTICES,&stream,&projection,NULL)==0);
    sh4dow_params_default(&sp);sp.dark=.5f;sp.length=10;sp.hold=1;sp.fade_in=0;
    CHECK(vid_mode->pm==PM_RGB565);CHECK(pvr_init(&params)==0);initialized=1;
    CHECK(pvr_txr_surface_alloc(&target,128,64,PVR_TXR_SURFACE_RGB565,PVR_TXR_SURFACE_LINEAR,false)==0);
    CHECK(sh4der_kos_frame_bind(&frame,&target,96,64,1)==0);CHECK(pvr_geometry_sink_init_current(&sink)==0);
    pvr_poly_cxt_col(&c,PVR_LIST_OP_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;pvr_poly_compile(&opaque,&c);
    pvr_poly_cxt_col(&c,PVR_LIST_TR_POLY);c.gen.culling=PVR_CULLING_NONE;c.gen.fog_type=PVR_FOG_DISABLE;
    c.depth.comparison=PVR_DEPTHCMP_ALWAYS;c.depth.write=PVR_DEPTHWRITE_DISABLE;c.blend.src=PVR_BLEND_SRCALPHA;c.blend.dst=PVR_BLEND_ONE;pvr_poly_compile(&add,&c);
    pvr_set_bg_color(0,0,0);
    for(unsigned pass=0;pass<10;++pass) {
        unsigned skin=pass==3?1:0;
        for(unsigned j=0;j<2;++j){memcpy(&joints[j],&identity,sizeof(matrix_t));joints[j][3][0]=pass==4?.04f:0;CHECK(pvr_normal_matrix_build(&normals[j],&joints[j])==0);}
        /* Child's world transform includes root translation; pose5 moves child away. */
        if(pass==5)joints[1][3][0]=3;
        CHECK(pvr_skin_palette_prepare(&raw_palette,imported,2,&palette)==0);
        pvr_deform_stream_t vertices={sources[skin].vertices,4,sizeof(*sources[skin].vertices)};
        CHECK(pvr_skin_apply_prepared(posed,4,&vertices,&weight_plans[skin],&palette,NULL)==0);
        CHECK(fabsf(posed[0].position.x-joints[skin][3][0])<.0001f);
        sh4dow_limb_t limb={{.175f+joints[1][3][0],0,0},1,1,0};
        CHECK(sh4dow_prepare(&shadow,&sp,&limb,1,direction)==1);
        base_context_t context={lookups[skin],pass>=6?&add:&opaque,pass==8};
        sh4der_kos_effect_t effect={.binding=&bindings[pass==1 || pass==8?1:pass==3?2:0],
            .mode=SH4DER_KOS_REPLACE,.base_prepare=base_prepare,.base_data=&context,
            .blade_rgb={.15f,.65f,1},.blade_alpha=255};
        if((pass>=2 && pass<=5) || pass==8){effect.shadow=&shadow;effect.shadow_axes=sh4dow_axes(&shadow);}
        if(pass==6 || pass==9)effect.blade=&blade;
        if(pass==7 || pass==8){effect.holo=&holo;effect.mode=pass==8?SH4DER_KOS_MODULATE:SH4DER_KOS_REPLACE;}
        CHECK(pvr_wait_ready()==0);CHECK(pvr_txr_surface_begin_render(&target,96,64)==0);
        CHECK(pvr_list_begin(pass>=6?PVR_LIST_TR_POLY:PVR_LIST_OP_POLY)==0);
        CHECK(pvr_chunk_model_emit_prepared(&plan,&projection,&sink,workspace,6,begin_strip,sh4der_kos_vertex,&effect,NULL)==0);
        if(pass==9){CHECK(pvr_prim(&add,sizeof(add))==0);CHECK(pvr_geometry_sink_emit(&sink,projected,SH4DER_KOS_BLOOM_VERTICES)==0);}
        CHECK(pvr_list_finish()==0);CHECK(pvr_scene_finish_tracked(&ticket)==0);CHECK(pvr_render_ticket_wait(&ticket,PVR_RENDER_STAGE_COMPLETE,1000)==0);
        CHECK(sh4der_kos_frame_read(&frame,&ticket,1,samples,NULL,4800,&id)==0);
        unsigned count=0,sumx=0,min=65535,max=0;for(unsigned y=0;y<60;++y)for(unsigned x=0;x<80;++x){unsigned p=samples[y*80+x];if(p){++count;sumx+=x;if(p<min)min=p;if(p>max)max=p;}}
        unsigned center=samples[30*80+40];float centroid=count?(float)sumx/count:0;
        printf("COMPACT pass=%u count=%u center=%04x min=%04x max=%04x centroid=%.3f\n",pass,count,center,min,max,(double)centroid);
        CHECK(count>500);
        if(pass==0){CHECK(center==0xffff);base_count=count;base_center=centroid;}
        if(pass==1)CHECK(center<0xffff && min<max && count==base_count);
        if(pass==2)CHECK(center>0x6b4d && center<0x9cf3);
        if(pass==3 || pass==5)CHECK(center==0xffff && count==base_count);
        if(pass==4)CHECK(centroid>base_center+5 && center<0x9cf3);
        if(pass==6){CHECK((center&31)>(center>>11));blade_count=count;}
        if(pass==7)CHECK(min<max && center>0);
        if(pass==8)CHECK(center>0 && center<0x7bef);
        if(pass==9)CHECK(count>blade_count);
    }
    pvr_txr_surface_release(&target);pvr_shutdown();puts("RESULT: PASS (SH4DER Compact poses)");return 0;
fail:
    if(initialized)pvr_shutdown();
    return 1;
}
