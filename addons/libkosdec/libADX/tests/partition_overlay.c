#include <kosdec/adx_partition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"overlay:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK KOSDEC_ADX_PARTITION_OK
#define NF KOSDEC_ADX_PARTITION_NOT_FOUND
#define BUSY KOSDEC_ADX_PARTITION_BUSY
struct fixture { uint8_t data[1024]; struct kosdec_adx_range ranges[12]; struct kosdec_adx_partition p; unsigned count; };
static void put(uint8_t *p,unsigned v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct fixture *f=ctx;
    CHECK(off<=sizeof(f->data));
    size_t n=sizeof(f->data)-off; if(n>cap)n=cap;
    memcpy(dst,f->data+off,n);
    return (struct kosdec_adx_input_result){n?KOSDEC_ADX_INPUT_DATA:KOSDEC_ADX_INPUT_END,n};
}
static void init(struct fixture *f,unsigned count,unsigned seed) {
    memset(f,0,sizeof(*f)); f->count=count; memcpy(f->data,"AFS",3);put(f->data+4,count);
    for(unsigned i=0;i<count;++i) {
        unsigned off=seed && (i+seed)%3==0?0:256+seed*32+i*4;
        unsigned bytes=off?((i+seed)%4):0;
        put(f->data+8+i*8,off); put(f->data+12+i*8,bytes);
        if(bytes)memset(f->data+off,(int)(seed*16+i),bytes);
    }
    CHECK(kosdec_adx_partition_init(&f->p,read_at,f,sizeof(f->data),f->ranges,12)==KOSDEC_ADX_MORE);
    enum kosdec_adx_status s; do{s=kosdec_adx_partition_step(&f->p);}while(s==KOSDEC_ADX_MORE);
    CHECK(s==KOSDEC_ADX_DONE);
}
/* Independent model stores chronological fixture indices, never native links. */
static void verify(struct kosdec_adx_partition_registry *r,struct fixture *f,
                   unsigned base,const unsigned *layers,unsigned depth) {
    unsigned newest=depth?layers[depth-1]:base;
    const struct kosdec_adx_partition *active=NULL;
    CHECK(kosdec_adx_partition_lookup(r,7,&active)==OK && active==&f[newest].p);
    for(unsigned id=0;id<14;++id) {
        unsigned selected=base; bool found=id<f[newest].count;
        if(found) {
            bool override=false;
            for(unsigned n=depth;n>0;--n) {
                unsigned k=layers[n-1];
                if(id<f[k].count && f[k].ranges[id].offset) {selected=k;override=true;break;}
            }
            if(!override && id>=f[base].count)found=false;
        }
        struct kosdec_adx_partition_resolution out={NULL,{123,456}};
        CHECK(kosdec_adx_partition_resolve(r,7,id,&out)==(found?OK:NF));
        if(found)CHECK(out.partition==&f[selected].p && out.range.offset==f[selected].ranges[id].offset && out.range.bytes==f[selected].ranges[id].bytes);
        else CHECK(!out.partition && out.range.offset==123 && out.range.bytes==456);
    }
}
static void semantics(void) {
    struct fixture f[4];init(f,4,0);init(f+1,8,1);init(f+2,2,2);init(f+3,12,3);
    struct kosdec_adx_partition_registry r,s;kosdec_adx_partition_registry_init(&r);kosdec_adx_partition_registry_init(&s);
    struct kosdec_adx_partition_overlay a,b,c;kosdec_adx_partition_overlay_init(&a);kosdec_adx_partition_overlay_init(&b);kosdec_adx_partition_overlay_init(&c);
    CHECK(kosdec_adx_partition_push(&r,7,&a,&f[1].p)==NF);
    CHECK(kosdec_adx_partition_bind(&r,7,&f[0].p)==OK);
    unsigned layers[]={1,2,3}; verify(&r,f,0,layers,0);
    CHECK(kosdec_adx_partition_push(&r,7,&a,&f[1].p)==OK);verify(&r,f,0,layers,1);
    CHECK(kosdec_adx_partition_push(&r,7,&b,&f[2].p)==OK);verify(&r,f,0,layers,2);
    CHECK(kosdec_adx_partition_push(&r,7,&c,&f[3].p)==OK);verify(&r,f,0,layers,3);
    CHECK(kosdec_adx_partition_bind(&s,9,&f[0].p)==OK);
    CHECK(kosdec_adx_partition_push(&s,9,&a,&f[1].p)==BUSY);
    CHECK(kosdec_adx_partition_bind(&r,7,&f[0].p)==OK);verify(&r,f,0,layers,3);
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_partition_close(&f[i].p)==BUSY);
    /* Resolved archive identity must select the overlay's provider. View remains
       usable after all metadata references and workspace lifetimes end. */
    struct kosdec_adx_partition_resolution res;
    CHECK(kosdec_adx_partition_resolve(&r,7,0,&res)==OK && res.partition==&f[2].p);
    struct kosdec_adx_view view;
    CHECK(kosdec_adx_partition_open_view(res.partition,0,read_at,&f[2],&view)==OK);
    struct kosdec_adx_partition_overlay *removed=NULL;
    CHECK(kosdec_adx_partition_pop(&r,7,&removed)==OK && removed==&c && !c.owner);
    verify(&r,f,0,layers,2);
    CHECK(kosdec_adx_partition_pop(&r,7,NULL)==OK);verify(&r,f,0,layers,1);
    CHECK(kosdec_adx_partition_push(&s,9,&b,&f[2].p)==OK);
    CHECK(kosdec_adx_partition_bind(&r,7,&f[3].p)==OK && !a.owner && !a.partition && !a.previous);
    CHECK(kosdec_adx_partition_registry_close(&r)==OK);
    CHECK(kosdec_adx_partition_unbind(&s,9)==OK && !b.owner);
    CHECK(kosdec_adx_partition_registry_close(&s)==OK);
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_partition_close(&f[i].p)==OK);
    uint8_t data[4]={0};struct kosdec_adx_input_result in=kosdec_adx_view_read_at(&view,0,data,sizeof(data));
    CHECK(in.status==KOSDEC_ADX_INPUT_DATA && in.bytes==2 && data[0]==32 && data[1]==32);
}
static uint32_t rng=1234567;
static unsigned random_value(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void randomized(void) {
    struct fixture f[5];unsigned counts[]={4,0,2,8,12};
    for(unsigned i=0;i<5;++i)init(f+i,counts[i],i);
    struct kosdec_adx_partition_registry r;kosdec_adx_partition_registry_init(&r);
    struct kosdec_adx_partition_overlay nodes[32];for(unsigned i=0;i<32;++i)kosdec_adx_partition_overlay_init(nodes+i);
    unsigned layers[32],depth=0,base=0;CHECK(kosdec_adx_partition_bind(&r,7,&f[base].p)==OK);
    for(unsigned n=0;n<10000;++n) {
        unsigned op=random_value()%4;
        if(op<2 && depth<32) {
            unsigned k=random_value()%5;
            CHECK(kosdec_adx_partition_push(&r,7,nodes+depth,&f[k].p)==OK);layers[depth++]=k;
        } else if(op==2 && depth) {
            struct kosdec_adx_partition_overlay *out=NULL;
            CHECK(kosdec_adx_partition_pop(&r,7,&out)==OK && out==nodes+depth-1);--depth;
        } else if(op==3) {
            unsigned next=random_value()%5;
            CHECK(kosdec_adx_partition_bind(&r,7,&f[next].p)==OK);
            if(next!=base) {depth=0;base=next;}
        }
        verify(&r,f,base,layers,depth);
        for(unsigned k=0;k<5;++k) {
            unsigned refs=k==base;for(unsigned i=0;i<depth;++i)refs+=layers[i]==k;
            CHECK(f[k].p.registrations==refs);
        }
    }
    CHECK(kosdec_adx_partition_registry_close(&r)==OK);
    for(unsigned i=0;i<32;++i)CHECK(!nodes[i].owner && !nodes[i].partition && !nodes[i].previous);
    for(unsigned k=0;k<5;++k)CHECK(kosdec_adx_partition_close(&f[k].p)==OK);
}
static void failures(void) {
    struct fixture f;init(&f,4,0);struct kosdec_adx_partition_registry r;kosdec_adx_partition_registry_init(&r);
    struct kosdec_adx_partition_overlay o={0},*out=&o;struct kosdec_adx_partition_resolution res={NULL,{123,456}};
    CHECK(kosdec_adx_partition_pop(&r,7,&out)==NF && out==&o);
    CHECK(kosdec_adx_partition_bind(&r,7,&f.p)==OK);
    CHECK(kosdec_adx_partition_push(&r,7,&o,&f.p)==KOSDEC_ADX_PARTITION_BAD_ARGUMENT);
    kosdec_adx_partition_overlay_init(&o);
    CHECK(kosdec_adx_partition_push(&r,256,&o,&f.p)==KOSDEC_ADX_PARTITION_BAD_ARGUMENT && !o.owner);
    CHECK(kosdec_adx_partition_resolve(&r,256,0,&res)==KOSDEC_ADX_PARTITION_BAD_ARGUMENT && res.range.offset==123);
    struct kosdec_adx_partition pending;struct kosdec_adx_range entries[4];
    CHECK(kosdec_adx_partition_init(&pending,read_at,&f,sizeof(f.data),entries,4)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_partition_push(&r,7,&o,&pending)==KOSDEC_ADX_PARTITION_NOT_READY && !o.owner);
    CHECK(kosdec_adx_partition_push(&r,7,&o,&f.p)==OK);
    CHECK(kosdec_adx_partition_bind(&r,7,&pending)==KOSDEC_ADX_PARTITION_NOT_READY && o.owner==&r);
    CHECK(kosdec_adx_partition_registry_close(&r)==OK && !o.owner);
    CHECK(kosdec_adx_partition_close(&f.p)==OK);CHECK(kosdec_adx_partition_close(&pending)==OK);
}
int main(void) {semantics();randomized();failures();printf("partition overlays: %u checks passed\n",checks);return 0;}
