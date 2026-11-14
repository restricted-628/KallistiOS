#include <sh4der_kos_trail.h>
#include <sh4der_kos_look.h>
#include <sh4der_kos_glow.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static pvr_poly_cxt_t contexts[32];
static unsigned compiled,admissions;
static int stage_error;
static pvr_render_stage_t stage=PVR_RENDER_STAGE_COMPLETE;
static alignas(32) uint16_t pixels[4096];
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *s,pvr_ptr_t *p) {++admissions;*p=s->vram;return 0;}
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *s) {return (s->format==PVR_TXR_SURFACE_RGB565?PVR_TXRFMT_RGB565:PVR_TXRFMT_ARGB4444)|PVR_TXRFMT_NONTWIDDLED;}
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *t,pvr_render_stage_t *p) {(void)t;if(stage_error){errno=ENOENT;return -1;}*p=stage;return 0;}
int pvr_txr_surface_upload(const pvr_txr_surface_t *s,const void *p,size_t n,pvr_txr_transfer_t method) {(void)s;(void)p;(void)n;(void)method;return 0;}
void pvr_poly_cxt_col(pvr_poly_cxt_t *c,pvr_list_t list) {memset(c,0,sizeof(*c));c->list_type=list;}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *c,pvr_list_t list,int f,int w,int h,pvr_ptr_t p,pvr_filter_mode_t filter) {
    pvr_poly_cxt_col(c,list);c->txr.enable=true;c->txr.format=f;c->txr.width=w;c->txr.height=h;c->txr.base=p;c->txr.filter=filter;
}
void pvr_poly_compile(pvr_poly_hdr_t *h,const pvr_poly_cxt_t *c) {assert(compiled<32);contexts[compiled]=*c;memset(h,0,sizeof(*h));h->words[0]=compiled++;}
static void look_tests(const sh4der_kos_overlay_t *overlay) {
    static uint16_t grid[4800],saved[4800];
    sh4der_kos_look_plan_t plan,oldplan;
    sh4der_kos_look_state_t state,oldstate;
    sh4der_kos_frame_history_t history;
    assert(sh4der_kos_look_prepare(&plan,NULL)==0);oldplan=plan;
    sh4der_look_params_t bad=plan.params;bad.light_s=NAN;
    assert(sh4der_kos_look_prepare(&plan,&bad)==-1 && !memcmp(&plan,&oldplan,sizeof(plan)));
    assert(sh4der_kos_look_reset(&state,7)==0);
    assert(sh4der_kos_frame_history_reset(&history,7)==0);
    sh4der_kos_frame_metadata_t m={.id={7,1},.gain=.5f,.gain_ln=sh4der_look_ln_(.5f)};
    assert(sh4der_kos_frame_history_put(&history,&m)==0);
    for(unsigned i=0;i<4800;++i)grid[i]=0x4208;
    /* Delayed sample must remove its .5 gain, not the current eye's1.5 gain. */
    state.eye=(sh4der_look_eye_t){.adapted=-1,.gain=1.5f,.gain_ln=sh4der_look_ln_(1.5f),.ready=1};
    sh4der_look_eye_t ref=state.eye;ref.gain_ln=m.gain_ln;
    uint16_t histogram[64]={0};histogram[sh4der_look_luma565(grid[0])]=4800;
    sh4der_look_eye_step(&ref,&plan.params,sh4der_look_log_average(&plan.bins,histogram,4800),.1f);
    oldstate=state;
    assert(sh4der_kos_look_update(&plan,&state,grid,4799,&history,m.id,.1f)==-1 && !memcmp(&state,&oldstate,sizeof(state)));
    assert(sh4der_kos_look_update(&plan,&state,grid,4800,&history,m.id,.1f)==0);
    assert(state.eye.gain==ref.gain && state.eye.adapted==ref.adapted);oldstate=state;
    assert(sh4der_kos_look_update(&plan,&state,grid,4800,&history,m.id,.1f)==-1 && errno==ESTALE);
    m.id.render=9;assert(sh4der_kos_frame_history_put(&history,&m)==0);
    state.last.render=0;oldstate=state;
    assert(sh4der_kos_look_update(&plan,&state,grid,4800,&history,(sh4der_kos_frame_id_t){7,1},.1f)==-1 && errno==ENOENT);
    assert(!memcmp(&state,&oldstate,sizeof(state)));
    const float rect[4]={0,0,96,64};sh4der_kos_screen_packet_t cached[6],direct[6];
    sh4der_look_grade_t grade={{.7f,1.4f,.9f},{-.1f,.1f,0},1.1f,.9f};
    unsigned before=compiled;
    int n=sh4der_kos_grade_packets_cached(overlay,&grade,rect,10,1,cached,6);assert(n>0 && compiled==before);
    assert(sh4der_kos_grade_packets(&grade,rect,10,1,direct,6)==n);
    for(int i=0;i<n;++i){assert(!memcmp(cached[i].vertices,direct[i].vertices,sizeof(cached[i].vertices)));assert(!memcmp(&contexts[cached[i].header.words[0]],&contexts[direct[i].header.words[0]],sizeof(pvr_poly_cxt_t)));}
    static sh4der_kos_glow_stage_t maps;
    maps.source=(sh4der_kos_frame_id_t){7,2};
    for(unsigned i=0;i<8192;++i)maps.big[i]=(2<<11)|(4<<5)|2;
    for(unsigned i=0;i<512;++i)maps.small[i]=(2<<11)|(4<<5)|2;
    m.id.render=3;m.gain=1;
    sh4der_kos_glow_feedback_t feedback;
    assert(sh4der_kos_glow_feedback_record(&feedback,&m,&maps,2,1,.5f)==0);
    for(unsigned i=0;i<4800;++i)grid[i]=(10<<11)|(20<<5)|10;
    memcpy(saved,grid,sizeof(grid));
    assert(sh4der_kos_glow_feedback_remove(&feedback,(sh4der_kos_frame_id_t){7,4},grid,4800)==-1 && errno==ESTALE && !memcmp(grid,saved,sizeof(grid)));
    assert(sh4der_kos_glow_feedback_remove(&feedback,m.id,grid,4799)==-1 && !memcmp(grid,saved,sizeof(grid)));
    assert(sh4der_kos_glow_feedback_remove(&feedback,m.id,grid,4800)==0);
    for(unsigned i=0;i<4800;++i)assert(grid[i]==((8<<11)|(16<<5)|8));
    memcpy(saved,grid,sizeof(grid));maps.source.render=4;
    assert(sh4der_kos_glow_feedback_remove(&feedback,m.id,grid,4800)==-1 && errno==ESTALE && !memcmp(grid,saved,sizeof(grid)));
    assert(sh4der_kos_glow_feedback_record(&feedback,&m,&maps,2,1,1)==-1);
}
int main(void) {
    pvr_txr_surface_t surface={.vram=pixels,.width=128,.height=64,.byte_size=16384,.format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
    sh4der_kos_image_t image;sh4der_kos_overlay_t overlay;
    sh4der_kos_trail_history_t history,old;
    assert(sh4der_kos_image_bind(&image,&surface,96,64,PVR_FILTER_NEAREST,false,true)==0);
    assert(sh4der_kos_overlay_prepare(&overlay)==0);
    memset(&history,0xa5,sizeof(history));old=history;
    assert(sh4der_kos_trail_bind(&history,&image,96,64,0)==-1 && !memcmp(&old,&history,sizeof(old)));
    assert(sh4der_kos_trail_bind(&history,&image,96,64,9)==0);
    pvr_render_ticket_t ticket={.id=100,.target=pixels,.width=96,.height=64,.stride=128,.to_texture=1};
    stage=PVR_RENDER_STAGE_QUEUED;
    assert(sh4der_kos_trail_publish(&history,&ticket,9)==0);old=history;
    assert(sh4der_kos_trail_publish(&history,&ticket,9)==-1 && !memcmp(&old,&history,sizeof(old)));
    ticket.id=101;ticket.width=95;
    assert(sh4der_kos_trail_publish(&history,&ticket,9)==-1);ticket.width=96;
    assert(sh4der_kos_trail_publish(&history,&ticket,8)==-1);
    stage_error=1;assert(sh4der_kos_trail_publish(&history,&ticket,9)==-1 && errno==ENOENT);stage_error=0;
    assert(!memcmp(&old,&history,sizeof(old)));
    sh4der_kos_screen_packet_t out[4],saved[4];
    const float rect[4]={0,0,96,64};pvr_ptr_t target=pixels+32;
    sh4der_trail_frame_t f={.weight=.5f,.copy_w=.5f/3,.copies=3,.dx={0,2,4},.dy={0,-1,-2}};
    unsigned c=compiled,a=admissions;
    memset(out,0xa5,sizeof(out));memcpy(saved,out,sizeof(out));
    assert(sh4der_kos_trail_packets(&history,history.id,target,&overlay,&f,rect,10,1,out,3)==-1 && errno==ENOSPC);
    assert(!memcmp(out,saved,sizeof(out)));
    assert(sh4der_kos_trail_packets(&history,history.id,pixels,&overlay,&f,rect,10,1,out,4)==-1);
    sh4der_kos_frame_id_t wrong={8,100};
    assert(sh4der_kos_trail_packets(&history,wrong,target,&overlay,&f,rect,10,1,out,4)==-1 && errno==ESTALE);
    f.copy_w=.2f;assert(sh4der_kos_trail_packets(&history,history.id,target,&overlay,&f,rect,10,1,out,4)==-1);f.copy_w=.5f/3;
    assert(sh4der_kos_trail_packets(&history,history.id,target,&overlay,&f,rect,1e30f,1,out,4)==-1);
    assert(!memcmp(out,saved,sizeof(out)));
    assert(sh4der_kos_trail_packets(&history,history.id,target,&overlay,&f,rect,10,1,out,4)==4);
    assert(out[0].vertices[0].argb==0x808080);
    for(unsigned i=0;i<3;++i){assert(out[i+1].vertices[0].x==f.dx[i]);assert(out[i+1].vertices[0].y==f.dy[i]);assert(out[i+1].vertices[0].argb==0x2b2b2b);assert(out[i+1].vertices[0].z==11+i);}
    sh4der_taa_frame_t t={.weight=.5f,.copy_px={2,-1}};
    assert(sh4der_kos_taa_packets(&history,history.id,target,&overlay,&t,rect,10,1,out,4)==2);
    assert(out[1].vertices[0].x==2 && out[1].vertices[0].y==-1);
    sh4der_kos_frame_id_t prior=history.id;
    sh4der_kos_trail_invalidate(&history);
    assert(sh4der_kos_taa_packets(&history,prior,target,&overlay,&t,rect,10,1,out,4)==-1 && errno==ESTALE);
    t.cut=1;assert(sh4der_kos_taa_packets(NULL,prior,NULL,&overlay,&t,rect,10,1,out,0)==0);
    assert(sh4der_kos_trail_publish(&history,&ticket,9)==0);
    assert(sh4der_kos_taa_packets(&history,prior,target,&overlay,&t,rect,10,1,out,4)==0);
    assert(compiled==c && admissions==a);
    assert(sh4der_kos_trail_bind(&history,&image,80,60,10)==0 && history.id.render==0);
    assert(sh4der_kos_trail_publish(&history,&ticket,9)==-1);
    ticket.width=80;ticket.height=60;assert(sh4der_kos_trail_publish(&history,&ticket,10)==0);
    sh4der_taa_t camera;sh4der_taa_init(&camera);sh4der_taa_step(&camera,0,0,48,32,&t);assert(t.cut);
    sh4der_taa_step(&camera,0,0,48,32,&t);assert(!t.cut && t.weight>0);
    sh4der_taa_reset(&camera);sh4der_taa_step(&camera,0,0,48,32,&t);assert(t.cut);
    look_tests(&overlay);
    puts("SH4DER temporal/look/feedback host PASS");return 0;
}
