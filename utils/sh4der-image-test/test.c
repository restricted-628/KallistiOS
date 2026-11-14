#include <sh4der_kos_focus.h>
#include <sh4der_kos_warp.h>
#include <sh4der_kos_ca.h>
#include <sh4der_kos_shafts.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned admissions, formats, compiled, uploads, queries;
static int address_error, format_error, upload_error, query_error;
static pvr_render_stage_t progress = PVR_RENDER_STAGE_COMPLETE;
static pvr_poly_cxt_t contexts[4];
static alignas(32) uint16_t vram[8192], blurred[8192], oldblur[8192];
static uint16_t grid[4800];
static pvr_vertex_t vertices[408], oldvertices[408];
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *s, pvr_ptr_t *p) {
    ++admissions;
    if(address_error) { errno=address_error; return -1; }
    *p=s->vram; return 0;
}
uint32_t pvr_txr_surface_pvr_format(const pvr_txr_surface_t *s) {
    ++formats;
    if(format_error) { errno=format_error; return UINT32_MAX; }
    return (s->format==PVR_TXR_SURFACE_RGB565 ? PVR_TXRFMT_RGB565 : PVR_TXRFMT_ARGB4444) |
           (s->layout==PVR_TXR_SURFACE_TWIDDLED ? 0 : PVR_TXRFMT_NONTWIDDLED) |
           (s->layout==PVR_TXR_SURFACE_STRIDE ? PVR_TXRFMT_X32_STRIDE : 0);
}
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *t, pvr_render_stage_t *p) {
    assert(t->id==12); ++queries;
    if(query_error) { errno=query_error; return -1; }
    *p=progress; return 0;
}
int pvr_txr_surface_upload(const pvr_txr_surface_t *s, const void *p, size_t n, pvr_txr_transfer_t transfer) {
    assert(n==s->byte_size && transfer>=PVR_TXR_TRANSFER_CPU && transfer<=PVR_TXR_TRANSFER_DMA);
    ++uploads;
    if(upload_error) { errno=upload_error; return -1; }
    memcpy(s->vram,p,n); return 0;
}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *c, pvr_list_t list, int f, int w, int h, pvr_ptr_t p, pvr_filter_mode_t filter) {
    memset(c,0,sizeof(*c)); c->list_type=list; c->txr.enable=true;
    c->txr.format=f; c->txr.width=w; c->txr.height=h; c->txr.base=p; c->txr.filter=filter;
}
void pvr_poly_compile(pvr_poly_hdr_t *h, const pvr_poly_cxt_t *c) {
    contexts[compiled%4]=*c; memset(h,0,sizeof(*h)); h->words[0]=compiled++%4;
}
static uint16_t blur_reference(unsigned x, unsigned y) {
    unsigned r=0,g=0,b=0;
    for(int j=-1;j<=1;++j) for(int i=-1;i<=1;++i) {
        int xx=(int)x+i, yy=(int)y+j;
        if(xx<0) xx=0;
        if(xx>79) xx=79;
        if(yy<0) yy=0;
        if(yy>59) yy=59;
        unsigned p=grid[yy*80+xx]; r+=p>>11; g+=(p>>5)&63; b+=p&31;
    }
    return (uint16_t)((((r+4)/9)<<11)|(((g+4)/9)<<5)|((b+4)/9));
}
static void warp_tests(void) {
    pvr_txr_surface_t surface={.vram=vram,.width=320,.height=256,.byte_size=sizeof(vram),
        .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_STRIDE};
    sh4der_kos_image_t image;
    assert(sh4der_kos_image_bind(&image,&surface,320,240,PVR_FILTER_BILINEAR,true,true)==0);
    unsigned before=admissions, before_compile=compiled;
    sh4der_kos_warp_grid_t topology={0}, oldtopology=topology;
    sh4der_kos_warp_scratch_t scratch;
    sh4der_warp_frame_t frame={0};
    static pvr_vertex_t mesh[2460], saved[2460];
    const float rect[4]={0,0,640,480}, center[2]={.5f,.5f};
    assert(sh4der_kos_warp_grid_prepare(&topology,0,30)==-1);
    assert(!memcmp(&topology,&oldtopology,sizeof(topology)));
    assert(sh4der_kos_warp_grid_prepare(&topology,40,30)==0);
    memset(mesh,0xa5,sizeof(mesh)); memcpy(saved,mesh,sizeof(mesh));
    assert(sh4der_kos_warp_mesh(&image,&topology,&frame,rect,1,&scratch,mesh,2459)==-1 && errno==ENOSPC);
    assert(!memcmp(mesh,saved,sizeof(mesh)));
    for(unsigned effect=0;effect<4;++effect) {
        frame.n=effect?1:0;
        frame.src[0]=(sh4der_warp_src_t){.kind=effect?effect-1:0,.cx=160,.cy=120,
            .x0=0,.y0=0,.x1=320,.y1=240,.age=.65f,.strength=1};
        assert(sh4der_kos_warp_mesh(&image,&topology,&frame,rect,1,&scratch,mesh,2460)==2460);
        unsigned n=0;
        for(unsigned j=0;j<30;++j) {
            sh4der_warp_vtx_t row0[41],row1[41];
            sh4der_warp_row(&frame,40,30,j,row0);sh4der_warp_row(&frame,40,30,j+1,row1);
            for(unsigned i=0;i<=40;++i) for(unsigned k=0;k<2;++k) {
                const sh4der_warp_vtx_t *r=k?&row1[i]:&row0[i];
                const pvr_vertex_t *v=&mesh[n++];
                assert(fabsf(v->u-r->u)<1e-6f && fabsf(v->v-r->v)<1e-6f);
                assert(v->x==r->x*2 && v->y==r->y*2 && v->z==1);
                assert(v->argb==0xffffffff && v->oargb==0);
                assert(v->flags==(i==40 && k?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
            }
        }
    }
    memcpy(saved,mesh,sizeof(mesh)); frame.n=5;
    assert(sh4der_kos_warp_mesh(&image,&topology,&frame,rect,1,&scratch,mesh,2460)==-1);
    frame.n=1;frame.src[0].age=1e20f;
    assert(sh4der_kos_warp_mesh(&image,&topology,&frame,rect,1,&scratch,mesh,2460)==-1 && errno==ERANGE);
    frame.src[0].age=0;frame.src[0].kind=SH4DER_WARP_RING;frame.src[0].cx=1e30f;
    assert(sh4der_kos_warp_mesh(&image,&topology,&frame,rect,1,&scratch,mesh,2460)==-1 && errno==ERANGE);
    assert(!memcmp(mesh,saved,sizeof(mesh)));
    /* Non-divisor grid rounds positions exactly like the public reference. */
    assert(sh4der_kos_warp_grid_prepare(&topology,7,11)==0);
    for(unsigned i=0;i<=7;++i) assert(topology.x[i]==(float)((i*320)/7));
    assert(topology.y[11]==240);
    sh4der_kos_screen_packet_t packets[4], oldpackets[4];
    memset(packets,0xa5,sizeof(packets));memcpy(oldpackets,packets,sizeof(packets));
    assert(sh4der_kos_warp_blur(&image,rect,center,1,4,1,1,packets,3)==-1 && errno==ENOSPC);
    assert(sh4der_kos_warp_blur(&image,rect,center,1,4,1e30f,1,packets,4)==-1 && errno==ERANGE);
    assert(!memcmp(packets,oldpackets,sizeof(packets)));
    assert(sh4der_kos_warp_blur(&image,rect,center,1,4,1,1,packets,4)==4);
    unsigned sum=0;
    for(unsigned i=0;i<4;++i) {
        sum+=packets[i].vertices[0].argb&255;
        assert(packets[i].header.words[0]==(i?SH4DER_KOS_IMAGE_ADD:SH4DER_KOS_IMAGE_COPY));
        assert(packets[i].vertices[0].z==1+i);
        assert(packets[i].vertices[0].x<=0 && packets[i].vertices[3].x>=640);
    }
    assert(sum==255);
    assert(sh4der_kos_warp_pixel(&image,rect,2,mesh,1199)==-1 && errno==ENOSPC);
    assert(sh4der_kos_warp_pixel(&image,rect,2,mesh,2460)==1200);
    for(unsigned y=0;y<15;++y) for(unsigned x=0;x<20;++x) {
        float uv[2];sh4der_warp_pixel_uv(x,y,uv);
        for(unsigned k=0;k<4;++k) {
            const pvr_vertex_t *v=&mesh[(y*20+x)*4+k];
            assert(fabsf(v->u-uv[0])<1e-6f && fabsf(v->v-uv[1])<1e-6f);
            assert(v->flags==(k==3?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
        }
    }
    uint16_t tile[64];sh4der_warp_dither_tile(tile);
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
        unsigned value=tile[sh4der_warp_twiddle8(x,y)];
        assert((value>>12)==15 && (value&15)>=11 && (value&15)<=15);
        assert((value&15)==((value>>4)&15) && (value&15)==((value>>8)&15));
    }
    assert(sh4der_kos_warp_dither(&image,rect,3,packets)==0);
    assert(packets[0].header.words[0]==SH4DER_KOS_IMAGE_MULTIPLY);
    assert(packets[0].vertices[3].u==40 && packets[0].vertices[3].v==30);
    assert(admissions==before && compiled==before_compile);
}
static void ca_shafts_tests(void) {
    const float rect[4]={0,0,640,480},sun[2]={.5f,.5f};
    pvr_txr_surface_t surface={.vram=vram,.width=128,.height=64,.byte_size=sizeof(vram),
        .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
    sh4der_kos_image_t image;
    assert(sh4der_kos_image_bind(&image,&surface,80,60,PVR_FILTER_BILINEAR,true,true)==0);
    sh4der_kos_ca_plan_t ca;
    static sh4der_kos_ca_packet_t packets[3], saved[3];
    assert(sh4der_kos_ca_prepare(&ca,rect)==0);
    assert(fabsf(ca.radius2[0][0]-1)<1e-6f && ca.radius2[3][4]==0);
    assert(sh4der_kos_ca_packets(&ca,&image,1,1,1,packets,3)==3);
    const int channels[3]={SH4DER_CA_GREEN,SH4DER_CA_RED,SH4DER_CA_BLUE};
    for(unsigned c=0;c<3;++c) {
        unsigned n=0;
        assert(packets[c].header.words[0]==(c?SH4DER_KOS_IMAGE_ADD:SH4DER_KOS_IMAGE_COPY));
        for(unsigned y=0;y<6;++y) for(unsigned x=0;x<=8;++x) for(unsigned k=0;k<2;++k) {
            float u=x/8.0f,v=(y+k)/6.0f,pu,pv;
            sh4der_ca_sample(sh4der_ca_push(channels[c],1,640,480),
                sh4der_ca_r2(u,v,640,480,4.0f/(640*640+480*480)),u,v,&pu,&pv);
            const pvr_vertex_t *p=&packets[c].vertices[n++];
            assert(fabsf(p->u-(.5f+pu*79)/128)<1e-6f && fabsf(p->v-(.5f+pv*59)/64)<1e-6f);
            assert(p->argb==sh4der_ca_argb(channels[c]) && p->z==1+c);
            assert(p->flags==(x==8 && k?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
        }
    }
    memcpy(saved,packets,sizeof(saved));
    assert(sh4der_kos_ca_packets(&ca,&image,1,1,1,packets,2)==-1 && errno==ENOSPC);
    assert(sh4der_kos_ca_packets(&ca,&image,NAN,1,1,packets,3)==-1);
    assert(sh4der_kos_ca_packets(&ca,&image,1,1e30f,1,packets,3)==-1 && errno==ERANGE);
    assert(!memcmp(saved,packets,sizeof(saved)));
    static sh4der_kos_shafts_plan_t shafts;
    static sh4der_kos_shafts_stage_t stage,oldstage;
    static uint16_t reference[8192];
    sh4der_kos_frame_id_t id={3,7};
    assert(sh4der_kos_shafts_prepare(&shafts,sun)==0);
    assert(shafts.near[30*80+40]==1 && shafts.near[0]<.1f);
    for(unsigned i=0;i<4800;++i) grid[i]=i%3?0:0xffff;
    sh4der_shafts_clear(reference,128,64);
    sh4der_shafts_map(reference,128,grid,.5f,.5f,sh4der_shafts_depth(1,sh4der_shafts_toward(1)));
    assert(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,1,1,id,NULL)==0);
    assert(!memcmp(stage.pixels,reference,sizeof(reference)));
    ++id.render;
    sh4der_shafts_map(reference,128,grid,.5f,.5f,sh4der_shafts_depth(1,sh4der_shafts_toward(1)));
    assert(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,1,1,id,&stage)==0);
    assert(!memcmp(stage.pixels,reference,sizeof(reference)));
    oldstage=stage;
    assert(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,1,1,id,&stage)==-1);
    ++id.generation;++id.render;
    assert(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,1,1,id,&stage)==-1);
    assert(!memcmp(&stage,&oldstage,sizeof(stage)));
    assert(sh4der_kos_shafts_map(&stage,&shafts,grid,4800,1,-1,id,NULL)==0);
    for(unsigned i=0;i<8192;++i) assert(stage.pixels[i]==0xffff);
    assert(sh4der_kos_shafts_mesh(&shafts,rect,10,.8f,vertices,107)==-1 && errno==ENOSPC);
    assert(sh4der_kos_shafts_mesh(&shafts,rect,10,.8f,vertices,408)==108);
    unsigned n=0;
    for(unsigned y=0;y<6;++y) for(unsigned x=0;x<=8;++x) for(unsigned k=0;k<2;++k) {
        float u,v;sh4der_glow_ray_uv(.8f,.5f,.5f,x/8.0f,(y+k)/6.0f,&u,&v);
        const pvr_vertex_t *p=&vertices[n++];
        assert(fabsf(p->u-u)<1e-6f && fabsf(p->v-v)<1e-6f && p->argb==0xffffffff);
        assert(p->flags==(x==8 && k?PVR_CMD_VERTEX_EOL:PVR_CMD_VERTEX));
    }
}
int main(void) {
    for(unsigned i=0;i<4800;++i) grid[i]=(uint16_t)(i*7919u+31u);
    memset(blurred,0xa5,sizeof(blurred)); memcpy(oldblur,blurred,sizeof(blurred));
    assert(sh4der_kos_focus_blur(grid,4799,blurred,8192)==-1 && errno==ENOSPC);
    assert(sh4der_kos_focus_blur(grid,4800,blurred,8191)==-1 && errno==ENOSPC);
    assert(!memcmp(blurred,oldblur,sizeof(blurred)));
    assert(sh4der_kos_focus_blur(grid,4800,blurred,8192)==0);
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x)
        assert(blurred[y*128+x]==blur_reference(x<80?x:79,y<60?y:59));
    sh4der_kos_focus_plan_t plan={0}, saved=plan;
    sh4der_focus_mask_t mask;
    sh4der_focus_mask_default(&mask,SH4DER_FOCUS_DISC);
    mask.feather=NAN;
    assert(sh4der_kos_focus_prepare(&plan,&mask)==-1 && !memcmp(&plan,&saved,sizeof(plan)));
    mask.feather=1; mask.hw=1e20f;
    assert(sh4der_kos_focus_prepare(&plan,&mask)==-1 && errno==ERANGE);
    const float rect[4]={10,20,320,240};
    for(int shape=0;shape<3;++shape) {
        sh4der_focus_mask_default(&mask,shape);
        assert(sh4der_kos_focus_prepare(&plan,&mask)==0);
        mask.cx=9999; /* snapshot owns its admitted values */
        assert(plan.mask.cx==320);
        memset(vertices,0xa5,sizeof(vertices)); memcpy(oldvertices,vertices,sizeof(vertices));
        assert(sh4der_kos_focus_mesh(&plan,rect,10,vertices,407)==-1 && errno==ENOSPC);
        assert(sh4der_kos_focus_mesh(&plan,rect,NAN,vertices,408)==-1);
        assert(!memcmp(vertices,oldvertices,sizeof(vertices)));
        assert(sh4der_kos_focus_mesh(&plan,rect,10,vertices,408)==408);
        unsigned n=0;
        for(unsigned j=0;j<12;++j) for(unsigned i=0;i<=16;++i) for(unsigned k=0;k<2;++k) {
            sh4der_focus_vtx_t reference;
            sh4der_focus_vertex(&plan.mask,i,j,k,&reference);
            const pvr_vertex_t *v=&vertices[n++];
            assert(v->argb==reference.argb && fabsf(v->u-reference.u)<1e-6f && fabsf(v->v-reference.v)<1e-6f);
            assert(fabsf(v->x-(10+reference.x*.5f))<1e-4f && fabsf(v->y-(20+reference.y*.5f))<1e-4f && v->z==10);
            assert(v->flags==(reference.eol ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX) && v->oargb==0);
        }
        assert(vertices[0].argb==0xffffffff && vertices[5*34+17].argb==0x00ffffff);
    }
    pvr_txr_surface_t surface={.vram=vram,.width=128,.height=64,.byte_size=sizeof(vram),
        .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
    sh4der_kos_image_t image={0}, previous=image;
    address_error=EFAULT;
    assert(sh4der_kos_image_bind(&image,&surface,80,60,PVR_FILTER_BILINEAR,false,true)==-1);
    assert(!memcmp(&image,&previous,sizeof(image)) && compiled==0); address_error=0;
    format_error=EINVAL;
    assert(sh4der_kos_image_bind(&image,&surface,80,60,PVR_FILTER_BILINEAR,false,true)==-1);
    assert(!memcmp(&image,&previous,sizeof(image)) && compiled==0); format_error=0;
    assert(sh4der_kos_image_bind(&image,&surface,129,60,PVR_FILTER_BILINEAR,false,true)==-1);
    assert(sh4der_kos_image_bind(&image,&surface,80,60,PVR_FILTER_BILINEAR,false,true)==0);
    assert(image.uv[0]==0 && image.uv[1]==0 && image.uv[2]==80.0f/128 && image.uv[3]==60.0f/64);
    for(unsigned i=0;i<4;++i) {
        const pvr_poly_cxt_t *c=&contexts[i];
        assert(c->list_type==(i?PVR_LIST_TR_POLY:PVR_LIST_OP_POLY));
        assert(c->gen.fog_type==PVR_FOG_DISABLE && c->gen.culling==PVR_CULLING_NONE);
        assert(c->depth.comparison==PVR_DEPTHCMP_ALWAYS && c->depth.write==PVR_DEPTHWRITE_DISABLE);
        assert(c->txr.width==128 && c->txr.height==64 && c->txr.base==vram);
        assert(c->txr.env==(i==1?PVR_TXRENV_MODULATEALPHA:PVR_TXRENV_MODULATE));
        assert(c->txr.filter==PVR_FILTER_BILINEAR && c->txr.uv_clamp==PVR_UVCLAMP_UV);
    }
    assert(contexts[0].blend.src==PVR_BLEND_ONE && contexts[0].blend.dst==PVR_BLEND_ZERO);
    assert(contexts[1].blend.src==PVR_BLEND_SRCALPHA && contexts[1].blend.dst==PVR_BLEND_INVSRCALPHA);
    assert(contexts[2].blend.src==PVR_BLEND_ONE && contexts[2].blend.dst==PVR_BLEND_ONE);
    assert(contexts[3].blend.src==PVR_BLEND_ZERO && contexts[3].blend.dst==PVR_BLEND_DESTCOLOR);
    unsigned count=admissions, fmtcount=formats;
    sh4der_kos_screen_packet_t quad, oldquad;
    memset(&quad,0xa5,sizeof(quad)); oldquad=quad;
    assert(sh4der_kos_image_quad(&image,9,rect,1,0xffffffff,&quad)==-1);
    assert(!memcmp(&quad,&oldquad,sizeof(quad)));
    for(unsigned i=0;i<4;++i) {
        assert(sh4der_kos_image_quad(&image,(sh4der_kos_image_mode_t)i,rect,1,0xff123456,&quad)==0);
        assert(quad.header.words[0]==i && quad.vertices[3].flags==PVR_CMD_VERTEX_EOL);
        assert(quad.vertices[3].u==80.0f/128 && quad.vertices[3].v==60.0f/64);
        assert(quad.vertices[0].argb==0xff123456 && quad.vertices[0].x==10);
    }
    assert(admissions==count && formats==fmtcount && compiled==4);
    pvr_render_ticket_t ticket={.id=12}; progress=PVR_RENDER_STAGE_RENDERING;
    assert(sh4der_kos_image_upload(&image,blurred,sizeof(blurred),&ticket,PVR_TXR_TRANSFER_DMA)==-1 && errno==EAGAIN && uploads==0);
    progress=PVR_RENDER_STAGE_COMPLETE; query_error=ENODEV;
    assert(sh4der_kos_image_upload(&image,blurred,sizeof(blurred),&ticket,PVR_TXR_TRANSFER_CPU)==-1 && errno==ENODEV && uploads==0);
    query_error=0; upload_error=EIO;
    assert(sh4der_kos_image_upload(&image,blurred,sizeof(blurred),&ticket,PVR_TXR_TRANSFER_CPU)==-1 && errno==EIO && uploads==1);
    upload_error=0;
    for(unsigned i=0;i<3;++i) assert(sh4der_kos_image_upload(&image,blurred,sizeof(blurred),&ticket,(pvr_txr_transfer_t)i)==0);
    assert(uploads==4 && queries==6 && !memcmp(vram,blurred,sizeof(vram)));
    surface.width=320; surface.height=256; surface.layout=PVR_TXR_SURFACE_STRIDE;
    assert(sh4der_kos_image_bind(&image,&surface,320,240,PVR_FILTER_NEAREST,true,false)==0);
    assert(contexts[0].txr.width==512 && (contexts[0].txr.format&PVR_TXRFMT_X32_STRIDE));
    assert(contexts[0].txr.uv_clamp==PVR_UVCLAMP_NONE);
    assert(image.uv[0]==.5f/512 && image.uv[2]==319.0f/512 && image.uv[3]==239.0f/256);
    surface.layout=PVR_TXR_SURFACE_VQ; previous=image;
    assert(sh4der_kos_image_bind(&image,&surface,320,240,PVR_FILTER_NEAREST,true,true)==-1);
    assert(!memcmp(&image,&previous,sizeof(image)));
    warp_tests();
    ca_shafts_tests();
    puts("SH4DER image/focus/warp/CA/shafts host PASS");
}
