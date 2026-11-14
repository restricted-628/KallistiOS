#include <sh4der_kos_glow.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned compiled, admissions, uploads, queries, fail_upload;
static int address_error, query_error;
static pvr_render_stage_t progress = PVR_RENDER_STAGE_COMPLETE;
static pvr_poly_cxt_t contexts[3];
int pvr_txr_surface_get_texture_address(const pvr_txr_surface_t *s, pvr_ptr_t *p) {
    ++admissions;
    if(address_error) { errno = address_error; return -1; }
    *p = s->vram; return 0;
}
int pvr_render_ticket_get_stage(const pvr_render_ticket_t *t, pvr_render_stage_t *p) {
    assert(t->id == 15); ++queries;
    if(query_error) { errno = query_error; return -1; }
    *p = progress; return 0;
}
int pvr_txr_surface_upload(const pvr_txr_surface_t *s, const void *p, size_t n, pvr_txr_transfer_t transfer) {
    assert(n == s->byte_size && ((uintptr_t)p & 31u) == 0);
    assert(transfer >= PVR_TXR_TRANSFER_CPU && transfer <= PVR_TXR_TRANSFER_DMA);
    if(++uploads == fail_upload) { errno = EIO; return -1; }
    memcpy(s->vram, p, n); return 0;
}
void pvr_poly_cxt_col(pvr_poly_cxt_t *c, pvr_list_t list) {
    memset(c, 0, sizeof(*c)); c->list_type = list;
}
void pvr_poly_cxt_txr(pvr_poly_cxt_t *c, pvr_list_t list, int f, int w, int h, pvr_ptr_t p, pvr_filter_mode_t filter) {
    pvr_poly_cxt_col(c,list); c->txr.enable = true; c->txr.format = f;
    c->txr.width = w; c->txr.height = h; c->txr.base = p; c->txr.filter = filter;
}
void pvr_poly_compile(pvr_poly_hdr_t *h, const pvr_poly_cxt_t *c) {
    assert(compiled < 3); contexts[compiled] = *c;
    memset(h, 0, sizeof(*h)); h->words[0] = compiled++;
}
static sh4der_kos_glow_stage_t maps, saved;
static uint16_t grid[SH4DER_KOS_FRAME_SAMPLES];
static alignas(32) uint16_t vram_big[8192], vram_small[512];
static void padding_zero(void) {
    for(unsigned y = 0; y < 64; ++y)
        for(unsigned x = 0; x < 128; ++x)
            if(y >= 60 || x >= 80) assert(maps.big[y * 128 + x] == 0);
    for(unsigned y = 0; y < 16; ++y)
        for(unsigned x = 0; x < 32; ++x)
            if(y >= 15 || x >= 20) assert(maps.small[y * 32 + x] == 0);
}
int main(void) {
    uint8_t lut[256]; memset(lut,255,sizeof(lut));
    sh4der_kos_frame_id_t source = {2, 4};
    memset(&maps,0xa5,sizeof(maps)); saved = maps;
    assert(sh4der_kos_glow_prepare(&maps,grid,4799,lut,source,NULL,0) == -1 && errno == ENOSPC);
    assert(!memcmp(&maps,&saved,sizeof(maps)));
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,NULL,128) == -1);
    assert(!memcmp(&maps,&saved,sizeof(maps)));
    /* Each 4x4 block has eight white/eight black pixels. */
    for(unsigned y = 0; y < 60; ++y)
        for(unsigned x = 0; x < 80; ++x) grid[y*80+x] = y % 4 < 2 ? 0xffff : 0;
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,NULL,0) == 0);
    assert(fabsf(maps.mean - 0.5f) < 0.00001f);
    for(unsigned y = 0; y < 60; ++y)
        for(unsigned x = 0; x < 80; ++x) assert(maps.big[y*128+x] == grid[y*80+x]);
    for(unsigned y = 0; y < 15; ++y)
        for(unsigned x = 0; x < 20; ++x) assert(maps.small[y*32+x] == 0x8410);
    padding_zero();
    /* Reset then ease black toward white in-place: rounded channel halves. */
    memset(grid,0,sizeof(grid));
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,NULL,0) == 0 && maps.mean == 0);
    memset(grid,255,sizeof(grid)); ++source.render;
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,&maps,128) == 0);
    assert(maps.big[0] == 0x8410 && maps.small[0] == 0x8410); padding_zero();
    saved = maps;
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,&maps,128) == -1);
    assert(!memcmp(&maps,&saved,sizeof(maps)));
    ++source.generation; ++source.render;
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,&maps,128) == -1);
    assert(!memcmp(&maps,&saved,sizeof(maps)));
    /* Host/load curve threshold: black never glows; white keeps full weight. */
    sh4der_glow_curve(lut,0.8f,0);
    for(unsigned i=0;i<4800;++i) grid[i] = i & 1 ? 0xffff : 0x7bef;
    assert(sh4der_kos_glow_prepare(&maps,grid,4800,lut,source,NULL,0) == 0);
    assert(maps.big[0] == 0 && maps.big[1] == 0xffff);

    pvr_txr_surface_t big = {.vram=vram_big,.byte_size=sizeof(vram_big),.width=128,.height=64,
        .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
    pvr_txr_surface_t small = {.vram=vram_small,.byte_size=sizeof(vram_small),.width=32,.height=16,
        .format=PVR_TXR_SURFACE_RGB565,.layout=PVR_TXR_SURFACE_LINEAR};
    sh4der_kos_glow_binding_t binding = {0}, oldbinding = binding;
    small.width=64;
    assert(sh4der_kos_glow_bind(&binding,&big,&small) == -1);
    assert(!memcmp(&binding,&oldbinding,sizeof(binding))); small.width=32;
    small.vram=vram_big;
    assert(sh4der_kos_glow_bind(&binding,&big,&small) == -1);
    assert(!memcmp(&binding,&oldbinding,sizeof(binding))); small.vram=vram_small;
    address_error=ENOSPC;
    assert(sh4der_kos_glow_bind(&binding,&big,&small) == -1 && errno==ENOSPC);
    address_error=0;
    assert(sh4der_kos_glow_bind(&binding,&big,&small) == 0 && !binding.ready);
    assert(compiled==3);
    for(unsigned i=0;i<3;++i) {
        pvr_poly_cxt_t *c=&contexts[i];
        assert(c->list_type==PVR_LIST_TR_POLY && c->gen.fog_type==PVR_FOG_DISABLE);
        assert(c->gen.culling==PVR_CULLING_NONE && c->depth.comparison==PVR_DEPTHCMP_ALWAYS);
        assert(c->depth.write==PVR_DEPTHWRITE_DISABLE);
        assert(c->blend.src==(i ? PVR_BLEND_ONE : PVR_BLEND_ZERO));
        assert(c->blend.dst==(i ? PVR_BLEND_ONE : PVR_BLEND_DESTCOLOR));
        if(i) {
            assert(c->txr.enable && c->txr.filter==PVR_FILTER_BILINEAR);
            assert(c->txr.env==PVR_TXRENV_MODULATE && c->txr.uv_clamp==PVR_UVCLAMP_UV);
            assert(c->txr.format==(PVR_TXRFMT_RGB565|PVR_TXRFMT_NONTWIDDLED));
            assert(c->txr.base==(i==1 ? (void *)vram_big : (void *)vram_small));
        }
    }
    sh4der_kos_screen_packet_t packets[3], oldpackets[3];
    const float rect[4]={10,20,320,240};
    memset(packets,0xa5,sizeof(packets)); memcpy(oldpackets,packets,sizeof(packets));
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,1,.5f,.25f,packets,3)==-1 && errno==EAGAIN);
    assert(sh4der_kos_glow_upload(&binding,&maps,NULL,PVR_TXR_TRANSFER_SQ)==0);
    assert(binding.ready && binding.source.render==source.render && uploads==2);
    assert(!memcmp(vram_big,maps.big,sizeof(vram_big)) && !memcmp(vram_small,maps.small,sizeof(vram_small)));
    unsigned before=admissions;
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,1,.5f,.25f,packets,2)==-1 && errno==ENOSPC);
    assert(!memcmp(packets,oldpackets,sizeof(packets)));
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,1,.5f,.25f,packets,3)==3);
    for(unsigned i=0;i<3;++i) {
        assert(packets[i].header.words[0]==i);
        for(unsigned k=0;k<4;++k) {
            pvr_vertex_t *v=&packets[i].vertices[k];
            assert(v->x==10+((k&1)?320:0) && v->y==20+((k&2)?240:0));
            assert(v->z==10+i && v->oargb==0);
            assert(v->flags==(k==3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
            assert(v->argb==(i ? 0xff808080 : 0xffbfbfbf));
        }
    }
    assert(packets[1].vertices[0].u==0.5f/128 && packets[1].vertices[3].u==79.5f/128);
    assert(packets[1].vertices[3].v==59.5f/64);
    assert(packets[2].vertices[0].u==0.5f/32 && packets[2].vertices[3].u==19.5f/32);
    assert(packets[2].vertices[3].v==14.5f/16);
    memcpy(oldpackets,packets,sizeof(packets));
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,0,0,0,packets,0)==0);
    assert(!memcmp(packets,oldpackets,sizeof(packets)));
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,NAN,0,0,packets,3)==-1);
    assert(sh4der_kos_glow_halo_packets(&binding,rect,1e30f,1,1,.5f,.25f,packets,3)==-1 && errno==ERANGE);
    assert(!memcmp(packets,oldpackets,sizeof(packets)) && admissions==before && compiled==3);
    pvr_render_ticket_t reader={.id=15}; oldbinding=binding;
    progress=PVR_RENDER_STAGE_RENDERING;
    assert(sh4der_kos_glow_upload(&binding,&maps,&reader,PVR_TXR_TRANSFER_DMA)==-1 && errno==EAGAIN);
    assert(uploads==2 && !memcmp(&binding,&oldbinding,sizeof(binding)));
    query_error=ENODEV;
    assert(sh4der_kos_glow_upload(&binding,&maps,&reader,PVR_TXR_TRANSFER_CPU)==-1 && errno==ENODEV);
    assert(uploads==2 && binding.ready); query_error=0; progress=PVR_RENDER_STAGE_COMPLETE;
    fail_upload=4; /* first succeeds; second fails: no mixed pair publication */
    assert(sh4der_kos_glow_upload(&binding,&maps,&reader,PVR_TXR_TRANSFER_DMA)==-1 && errno==EIO);
    assert(!binding.ready && uploads==4);
    assert(sh4der_kos_glow_halo_packets(&binding,rect,10,1,1,.5f,0,packets,3)==-1 && errno==EAGAIN);
    assert(!memcmp(packets,oldpackets,sizeof(packets)));
    fail_upload=0;
    assert(sh4der_kos_glow_upload(&binding,&maps,&reader,PVR_TXR_TRANSFER_DMA)==0 && binding.ready);
    assert(queries==4 && uploads==6);
    static sh4der_kos_glow_strip_t strips[36], oldstrips[36];
    const float sun[2]={.5f,.5f}, outside[2]={1e30f,1e30f};
    memset(strips,0xa5,sizeof(strips)); memcpy(oldstrips,strips,sizeof(strips));
    assert(sh4der_kos_glow_ray_packets(&binding,rect,10,1,sun,1,6,strips,35)==-1 && errno==ENOSPC);
    assert(!memcmp(strips,oldstrips,sizeof(strips)));
    assert(sh4der_kos_glow_ray_packets(&binding,rect,10,1,outside,1,6,strips,0)==0);
    assert(sh4der_kos_glow_ray_packets(&binding,rect,10,1,sun,0,6,strips,0)==0);
    assert(!memcmp(strips,oldstrips,sizeof(strips)));
    assert(sh4der_kos_glow_ray_packets(&binding,rect,1e30f,1,sun,1,6,strips,36)==-1 && errno==ERANGE);
    assert(!memcmp(strips,oldstrips,sizeof(strips)));
    assert(sh4der_kos_glow_ray_packets(&binding,rect,10,1,sun,1,6,strips,36)==36);
    assert(strips[0].vertices[0].argb==0xff000000);
    assert(strips[2].vertices[9].argb==0xff595959); /* actual mesh center survives */
    for(unsigned i=0;i<36;++i) {
        assert(strips[i].header.words[0]==1);
        for(unsigned j=0;j<18;++j) {
            pvr_vertex_t *v=&strips[i].vertices[j];
            assert(v->flags==(j==17 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX));
            assert(v->z==10+i/6 && v->oargb==0);
            assert(v->u>=.5f/128 && v->u<=79.5f/128);
            assert(v->v>=.5f/64 && v->v<=59.5f/64);
        }
    }
    assert(sh4der_kos_glow_shaft_packets(&binding,rect,20,sun,.5f,.5f,strips,6)==6);
    assert(strips[0].vertices[0].u==20.25f/128);
    assert(strips[0].vertices[0].v==15.25f/64);
    for(unsigned i=0;i<6;++i) for(unsigned j=0;j<18;++j)
        assert(strips[i].vertices[j].argb==0xff808080 && strips[i].vertices[j].z==20);
    memcpy(oldstrips,strips,sizeof(strips));
    assert(sh4der_kos_glow_shaft_packets(&binding,rect,20,sun,.5f,0,strips,0)==0);
    assert(sh4der_kos_glow_shaft_packets(&binding,rect,20,sun,.5f,.5f,strips,5)==-1 && errno==ENOSPC);
    assert(sh4der_kos_glow_shaft_packets(&binding,rect,20,sun,NAN,.5f,strips,6)==-1 && errno==EDOM);
    assert(!memcmp(strips,oldstrips,sizeof(strips)));
    assert(admissions==before && compiled==3);
    puts("PASS: glow rays/shaft complete strips, center fade, UVs, depth, capacity and no-op preservation");
    puts("PASS: glow maps, padding, easing, exact history generation, upload retirement/failure, halo state/order/UV/capacity");
    return 0;
}
