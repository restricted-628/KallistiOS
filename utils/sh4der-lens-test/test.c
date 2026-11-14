#include <sh4der_kos_lens.h>
#include <sh4der_kos_visor.h>
#include <sh4der_kos_torch.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static pvr_poly_cxt_t contexts[32];
static unsigned compiled,admissions;
static alignas(32) uint16_t pixels[4096];
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *s,pvr_ptr_t *p) {++admissions;*p=s->vram;return 0;}
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *s) {return (s->format==PVR_TXR_SURFACE_RGB565?PVR_TXRFMT_RGB565:PVR_TXRFMT_ARGB4444)|PVR_TXRFMT_NONTWIDDLED;}
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *t,pvr_render_stage_t *p) {(void)t;*p=PVR_RENDER_STAGE_COMPLETE;return 0;}
int pvr_txr_surface_upload(const pvr_txr_surface_t *s,const void *p,size_t n,pvr_txr_transfer_t method) {(void)s;(void)p;(void)n;(void)method;return 0;}
void pvr_poly_cxt_col(pvr_poly_cxt_t *c,pvr_list_t list) {memset(c,0,sizeof(*c));c->list_type=list;}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *c,pvr_list_t list,int f,int w,int h,pvr_ptr_t p,pvr_filter_mode_t filter) {
    pvr_poly_cxt_col(c,list);c->txr.enable=true;c->txr.format=f;c->txr.width=w;c->txr.height=h;c->txr.base=p;c->txr.filter=filter;
}
void pvr_poly_compile(pvr_poly_hdr_t *h,const pvr_poly_cxt_t *c) {assert(compiled<32);contexts[compiled]=*c;memset(h,0,sizeof(*h));h->words[0]=compiled++;}
static void visor_tests(const sh4der_kos_overlay_t *overlay) {
    static sh4der_kos_visor_plan_t plan,oldplan;
    static uint16_t grid[4800],thermal[8192],oldthermal[8192];
    static pvr_vertex_t mesh[408],oldmesh[408];
    sh4der_kos_image_t images[3];
    pvr_txr_surface_t surfaces[3];
    unsigned widths[3]={128,32,8},heights[3]={64,32,8};
    for(unsigned i=0;i<3;++i) {
        surfaces[i]=(pvr_txr_surface_t){.vram=pixels,.width=widths[i],.height=heights[i],.byte_size=widths[i]*heights[i]*2,
            .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
        assert(sh4der_kos_image_bind(&images[i],&surfaces[i],i?widths[i]:80,i?heights[i]:60,
            i==2?PVR_FILTER_NEAREST:PVR_FILTER_BILINEAR,false,i==0)==0);
    }
    unsigned count=compiled,checks=admissions;
    memset(&plan,0xa5,sizeof(plan));oldplan=plan;
    assert(sh4der_kos_visor_prepare(&plan,4)==-1 && !memcmp(&plan,&oldplan,sizeof(plan)));
    const float rect[4]={0,0,640,480};
    sh4der_kos_screen_packet_t packets[8],saved[8];
    for(int mode=0;mode<4;++mode) {
        assert(sh4der_kos_visor_prepare(&plan,mode)==0);
        memset(packets,0xa5,sizeof(packets));memcpy(saved,packets,sizeof(packets));
        assert(sh4der_kos_visor_packets(&plan,overlay,NULL,NULL,NULL,0,0,rect,10,1,packets,0)==0);
        assert(!memcmp(packets,saved,sizeof(packets)));
        int n=sh4der_kos_visor_packets(&plan,overlay,&images[0],&images[1],&images[2],1,10,rect,10,1,packets,8);
        assert(n>=0 && n<=8);
        if(!mode) assert(n==0);
        else {
            assert(n>1);
            for(int i=0;i<n;++i) assert(packets[i].vertices[0].z==10+i);
            memcpy(saved,packets,sizeof(packets));
            assert(sh4der_kos_visor_packets(&plan,overlay,&images[0],&images[1],&images[2],1,10,rect,10,1,packets,n-1)==-1 && errno==ENOSPC);
            assert(sh4der_kos_visor_packets(&plan,overlay,&images[0],&images[1],&images[2],1,10,rect,1e30f,1,packets,8)==-1 && errno==ERANGE);
            assert(!memcmp(packets,saved,sizeof(packets)));
        }
        memset(mesh,0xa5,sizeof(mesh));memcpy(oldmesh,mesh,sizeof(mesh));
        assert(sh4der_kos_visor_vignette(&plan,0,rect,20,mesh,0)==0 && !memcmp(mesh,oldmesh,sizeof(mesh)));
        if(mode) {
            assert(sh4der_kos_visor_vignette(&plan,.5f,rect,20,mesh,407)==-1 && errno==ENOSPC);
            assert(!memcmp(mesh,oldmesh,sizeof(mesh)));
            assert(sh4der_kos_visor_vignette(&plan,.5f,rect,20,mesh,408)==408);
            assert(mesh[0].argb>0 && mesh[5*34+17].argb==0);
            for(unsigned i=0;i<408;++i) assert((mesh[i].argb&0xffffff)==0 && mesh[i].u==0 && mesh[i].v==0);
        }
    }
    for(unsigned i=0;i<4800;++i)grid[i]=(uint16_t)(i*7919u);
    memset(thermal,0xa5,sizeof(thermal));memcpy(oldthermal,thermal,sizeof(thermal));
    assert(sh4der_kos_visor_thermal(&plan,grid,4799,thermal,8192)==-1 && errno==ENOSPC);
    assert(!memcmp(thermal,oldthermal,sizeof(thermal)));
    assert(sh4der_kos_visor_thermal(&plan,grid,4800,thermal,8192)==0);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<128;++x)
        assert(thermal[y*128+x]==plan.lut[sh4der_look_luma565(grid[(y<60?y:59)*80+(x<80?x:79)])]);
    assert(compiled==count && admissions==checks);
}
static void torch_tests(void) {
    sh4der_kos_torch_shape_t shape={320,240,100,80,.96f,.88f,.2f,5};
    static sh4der_kos_torch_plan_t plan,oldplan;
    static sh4der_torch_vert_t scratch[SH4DER_TORCH_VERTS];
    static pvr_vertex_t mesh[SH4DER_TORCH_VERTS],saved[SH4DER_TORCH_VERTS];
    memset(&plan,0xa5,sizeof(plan));oldplan=plan;
    assert(sh4der_kos_torch_prepare(&plan,&shape,scratch,305)==-1 && errno==ENOSPC);
    assert(!memcmp(&plan,&oldplan,sizeof(plan)));
    shape.rx=NAN;assert(sh4der_kos_torch_prepare(&plan,&shape,scratch,306)==-1);
    assert(!memcmp(&plan,&oldplan,sizeof(plan)));shape.rx=100;
    assert(sh4der_kos_torch_prepare(&plan,&shape,scratch,306)==0);
    memset(mesh,0xa5,sizeof(mesh));memcpy(saved,mesh,sizeof(mesh));
    assert(sh4der_kos_torch_mesh(&plan,0,0,1,2,mesh,0)==0 && !memcmp(mesh,saved,sizeof(mesh)));
    assert(sh4der_kos_torch_mesh(&plan,.37f,.71f,1,2,mesh,305)==-1 && errno==ENOSPC);
    assert(!memcmp(mesh,saved,sizeof(mesh)));
    assert(sh4der_kos_torch_mesh(&plan,.37f,.71f,1,2,mesh,306)==306);
    assert(sh4der_torch_carve_mesh_to(scratch,320,240,100,80,.71f,5)==136);
    for(unsigned i=0;i<136;++i) {
        assert(mesh[i].x==scratch[i].x && mesh[i].y==scratch[i].y && mesh[i].argb==scratch[i].argb && mesh[i].z==1);
        assert(mesh[i].flags==(scratch[i].eol?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
    }
    assert(sh4der_torch_boost_mesh_core(scratch,320,240,100,80,.37f,.96f,.88f,.2f)==170);
    for(unsigned i=0;i<170;++i) {
        assert(mesh[136+i].x==scratch[i].x && mesh[136+i].y==scratch[i].y && mesh[136+i].argb==scratch[i].argb && mesh[136+i].z==2);
        assert(mesh[136+i].flags==(scratch[i].eol?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
    }
    float base[129],fog[129],oldfog[129];
    sh4der_kos_torch_fog_plan_t fp;
    for(unsigned i=0;i<129;++i)base[i]=i/128.0f;
    assert(sh4der_kos_torch_fog_prepare(&fp,base,129,8)==0);
    memset(fog,0xa5,sizeof(fog));memcpy(oldfog,fog,sizeof(fog));
    assert(sh4der_kos_torch_fog(&fp,.7f,fog,128)==-1 && errno==ENOSPC);
    assert(sh4der_kos_torch_fog(&fp,NAN,fog,129)==-1 && !memcmp(fog,oldfog,sizeof(fog)));
    assert(sh4der_kos_torch_fog(&fp,.7f,fog,129)==0);
    for(unsigned i=0;i<129;++i)assert(fabsf(fog[i]-sh4der_torch_fog_bend(base[i],sh4der_torch_entry_depth(i,8),.7f))<1e-6f);
    assert(sh4der_kos_torch_fog(&fp,0,fog,129)==0);
    assert(!memcmp(fog,base,sizeof(fog)));
    assert(sh4der_torch_near_at(0,1,.5f,.25f)==.5f && sh4der_torch_near_at(4,1,.5f,.25f)==0);
}
int main(void) {
    sh4der_kos_overlay_t overlay;
    assert(sh4der_kos_overlay_prepare(&overlay)==0 && compiled==5);
    for(unsigned i=0;i<5;++i) {
        assert(contexts[i].gen.fog_type==PVR_FOG_DISABLE && contexts[i].depth.comparison==PVR_DEPTHCMP_ALWAYS);
        assert(contexts[i].depth.write==PVR_DEPTHWRITE_DISABLE && contexts[i].list_type==PVR_LIST_TR_POLY);
    }
    assert(contexts[0].blend.src==PVR_BLEND_ZERO && contexts[0].blend.dst==PVR_BLEND_DESTCOLOR);
    assert(contexts[1].blend.src==PVR_BLEND_DESTCOLOR && contexts[1].blend.dst==PVR_BLEND_ONE);
    assert(contexts[2].blend.src==PVR_BLEND_ONE && contexts[2].blend.dst==PVR_BLEND_ONE);
    assert(contexts[3].blend.src==PVR_BLEND_INVDESTCOLOR && contexts[3].blend.dst==PVR_BLEND_ZERO);
    assert(contexts[4].blend.src==PVR_BLEND_SRCALPHA && contexts[4].blend.dst==PVR_BLEND_INVSRCALPHA);
    float mean=-1;
    assert(sh4der_kos_lens_grain_bake(pixels,4095,13,&mean)==-1 && mean==-1);
    assert(sh4der_kos_lens_grain_bake(pixels,4096,13,&mean)==0);
    unsigned sum=0;
    for(unsigned i=0;i<4096;++i) {unsigned l=sh4der_lens_grain_level(i,13);assert(pixels[i]==(0xf000u|l*0x111));sum+=l;}
    assert(fabsf(mean-(float)sum/(15*4096))<1e-6f);
    pvr_txr_surface_t s={.vram=pixels,.width=64,.height=64,.byte_size=8192,.layout=PVR_TXR_SURFACE_LINEAR,.format=PVR_TXR_SURFACE_ARGB4444};
    sh4der_kos_lens_grain_t grain={0},oldgrain=grain;
    assert(sh4der_kos_lens_grain_bind(&grain,&s,1,0)==-1 && errno==ERANGE);
    assert(!memcmp(&grain,&oldgrain,sizeof(grain)));
    assert(sh4der_kos_lens_grain_bind(&grain,&s,.25f,mean)==0 && admissions==1 && compiled==10);
    const pvr_poly_cxt_t *c=&contexts[9];
    assert(c->gen.specular==true && c->txr.env==PVR_TXRENV_MODULATE);
    assert(c->txr.uv_clamp==PVR_UVCLAMP_NONE && c->txr.filter==PVR_FILTER_NEAREST);
    assert(c->blend.src==PVR_BLEND_DESTCOLOR && c->blend.dst==PVR_BLEND_ZERO);
    assert(grain.amplitude==0xff404040 && grain.offset==0xffbfbfbf);
    sh4der_kos_screen_packet_t packets[4],old[4];
    const float rect[4]={10,20,640,480};
    memset(packets,0xa5,sizeof(packets));memcpy(old,packets,sizeof(old));
    assert(sh4der_kos_lens_grain_packets(&grain,&overlay,17,rect,10,1,packets,1)==-1 && errno==ENOSPC);
    assert(sh4der_kos_lens_grain_packets(&grain,&overlay,17,rect,1e30f,1,packets,4)==-1 && errno==ERANGE);
    assert(!memcmp(packets,old,sizeof(old)));
    assert(sh4der_kos_lens_grain_packets(&grain,&overlay,17,rect,10,1,packets,4)==2);
    float uv[4];sh4der_lens_grain_uv(17,640,480,uv);
    assert(packets[0].header.words[0]==9 && packets[1].header.words[0]==1);
    for(unsigned k=0;k<4;++k) {
        assert(packets[0].vertices[k].oargb==grain.offset && packets[0].vertices[k].argb==grain.amplitude);
        assert(packets[0].vertices[k].u==uv[k&1?2:0] && packets[0].vertices[k].v==uv[k&2?3:1]);
    }
    sh4der_lens_hit_t slots[4]={0};
    assert(sh4der_lens_hit_add(slots,4,1,0)==1);sh4der_lens_hit_tick(slots,4,.5f);
    assert(sh4der_kos_lens_hits(&overlay,slots,4,rect,10,1,packets,4)==1);
    assert(packets[0].header.words[0]==4 && packets[0].vertices[0].x==650);
    assert(packets[0].vertices[0].argb>>24==204 && packets[0].vertices[3].argb>>24==0);
    sh4der_lens_hit_tick(slots,4,2);memcpy(old,packets,sizeof(old));
    assert(sh4der_kos_lens_hits(&overlay,slots,4,rect,10,1,packets,0)==0 && !memcmp(packets,old,sizeof(old)));
    const float rgb[3]={.2f,.4f,1};
    assert(sh4der_kos_lens_flash(&overlay,0,rgb,true,rect,10,packets)==0 && !memcmp(packets,old,sizeof(old)));
    assert(sh4der_kos_lens_flash(&overlay,.5f,rgb,false,rect,10,packets)==1);
    assert(packets[0].vertices[0].argb==0x803366ff && packets[0].header.words[0]==4);
    assert(sh4der_kos_lens_flash(&overlay,.5f,rgb,true,rect,10,packets)==1);
    assert(packets[0].vertices[0].argb==0x001a3380 && packets[0].header.words[0]==2);
    sh4der_lens_vig_t vig;sh4der_lens_vig_default(&vig,640,480);
    static sh4der_lens_vtx_t scratch[SH4DER_KOS_LENS_VIG_MAX];
    static pvr_vertex_t mesh[SH4DER_KOS_LENS_VIG_MAX],saved[SH4DER_KOS_LENS_VIG_MAX];
    memset(mesh,0xa5,sizeof(mesh));memcpy(saved,mesh,sizeof(mesh));
    assert(sh4der_kos_lens_vignette(&vig,10,scratch,1122,mesh,593)==-1 && errno==ENOSPC);
    assert(!memcmp(mesh,saved,sizeof(mesh)));
    assert(sh4der_kos_lens_vignette(&vig,10,scratch,1122,mesh,1122)==594);
    for(unsigned i=0;i<594;++i) {
        assert(mesh[i].x==scratch[i].x && mesh[i].y==scratch[i].y && mesh[i].argb==scratch[i].argb);
        assert(mesh[i].flags==(i%66==65?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
    }
    assert(compiled==10 && admissions==1);
    visor_tests(&overlay);
    torch_tests();
    puts("SH4DER lens/visor/torch host PASS");
}
