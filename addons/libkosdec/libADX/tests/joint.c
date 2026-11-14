#include <kosdec/adx_joint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr, "joint:%d: %s\n", __LINE__, #x); exit(1); } } while(0)
#define OK(x) CHECK((x) == KOSDEC_ADX_JOINT_OK)
#define FREE KOSDEC_ADX_JOINT_FREE
#define DATA KOSDEC_ADX_JOINT_DATA
static uint32_t rng = 13;
static unsigned random_n(unsigned n) {
    rng = rng * UINT32_C(1664525) + UINT32_C(1013904223);
    return rng % n;
}
static unsigned error_calls;
static void error(void *ctx, enum kosdec_adx_joint_status status) {
    CHECK(ctx == &error_calls && status != KOSDEC_ADX_JOINT_OK);
    ++error_calls;
}
static void unchanged(struct kosdec_adx_joint *j, struct kosdec_adx_joint before) {
    CHECK(memcmp(j, &before, sizeof(*j)) == 0);
}
static void memory(void) {
    uint8_t bytes[11];
    for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(uint8_t)i;
    struct kosdec_adx_joint j, other = {0};
    struct kosdec_adx_chunk c, a, b, empty = {0};
    struct kosdec_adx_joint_info info;
    OK(kosdec_adx_joint_memory_init(&j, bytes, sizeof(bytes)));
    OK(kosdec_adx_joint_set_error(&j, error, &error_calls));
    OK(kosdec_adx_joint_query(&j, &info));
    CHECK(info.kind == KOSDEC_ADX_JOINT_MEMORY && info.available[DATA]==11);
    OK(kosdec_adx_joint_get(&j, FREE, 10, &c)); CHECK(!c.bytes && !c.data);
    OK(kosdec_adx_joint_get(&j, DATA, 9, &c));
    CHECK(c.bytes==9 && !memcmp(c.data, bytes, 9));
    struct kosdec_adx_joint before=j;
    CHECK(kosdec_adx_joint_reset(&j)==KOSDEC_ADX_JOINT_BUSY); unchanged(&j,before);
    CHECK(kosdec_adx_joint_close(&j)==KOSDEC_ADX_JOINT_BUSY); unchanged(&j,before);
    a=c;
    CHECK(kosdec_adx_joint_get(&j, DATA, 1, &a)==KOSDEC_ADX_JOINT_BUSY);
    CHECK(a.token==c.token && a.bytes==c.bytes); unchanged(&j,before);
    OK(kosdec_adx_joint_get(&j, DATA, 0, &a)); CHECK(!a.bytes); unchanged(&j,before);
    OK(kosdec_adx_joint_put(&j, &empty)); OK(kosdec_adx_joint_unget(&j, &empty));
    unchanged(&j,before);
    a=c; a.owner=&other;
    CHECK(kosdec_adx_joint_put(&j,&a)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    a=c; ++a.bytes;
    CHECK(kosdec_adx_joint_put(&j,&a)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    OK(kosdec_adx_chunk_split(&c, 4, &a, &b));
    CHECK(kosdec_adx_joint_put(&j,&b)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    CHECK(kosdec_adx_joint_unget(&j,&a)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    OK(kosdec_adx_joint_unget(&j,&b));
    OK(kosdec_adx_joint_put(&j,&a));
    before=j;
    CHECK(kosdec_adx_joint_put(&j,&a)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    OK(kosdec_adx_joint_query(&j,&info));
    CHECK(info.available[DATA]==7 && info.available[FREE]==0);
    OK(kosdec_adx_joint_get(&j,DATA,SIZE_MAX,&c));
    CHECK(c.bytes==7 && c.data==bytes+4);
    OK(kosdec_adx_joint_put(&j,&c));
    OK(kosdec_adx_joint_reset(&j));
    OK(kosdec_adx_joint_get(&j,DATA,4,&b));
    before=j;
    CHECK(kosdec_adx_joint_put(&j,&a)==KOSDEC_ADX_JOINT_STALE); unchanged(&j,before);
    OK(kosdec_adx_joint_put(&j,&b));
    OK(kosdec_adx_joint_set_error(&j,NULL,&j)); CHECK(!j.error_context);
    CHECK(error_calls==9);
    OK(kosdec_adx_joint_close(&j));
    CHECK(kosdec_adx_joint_reset(&j)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_query(&j,&info)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    for(unsigned i=0;i<sizeof(bytes);++i) CHECK(bytes[i]==i);
}
/* A byte FIFO model independent of wrap/mirror implementation. Vary every
   small capacity/extra combination and split both sides in either order. */
static void ring_model(unsigned cap, unsigned extra) {
    uint8_t storage[65], queue[32];
    memset(storage,0xa5,sizeof(storage));
    struct kosdec_adx_joint j;
    OK(kosdec_adx_joint_ring_init(&j,storage+1,cap,extra));
    size_t queued=0;
    unsigned generated=0;
    for(unsigned step=0;step<400;++step) {
        enum kosdec_adx_joint_line line=random_n(2) ? DATA : FREE;
        struct kosdec_adx_joint_info info;
        struct kosdec_adx_chunk c,a,b,part;
        OK(kosdec_adx_joint_query(&j,&info));
        CHECK(info.available[DATA]==queued && info.available[FREE]==cap-queued);
        size_t request=random_n(cap+7);
        size_t expected=request < info.contiguous[line] ? request : info.contiguous[line];
        OK(kosdec_adx_joint_get(&j,line,request,&c));
        CHECK(c.bytes==expected);
        if(line==DATA) CHECK(!c.bytes || !memcmp(c.data,queue,c.bytes));
        else for(size_t i=0;i<c.bytes;++i) c.data[i]=(uint8_t)(generated+i);
        size_t used=random_n((unsigned)c.bytes+1);
        OK(kosdec_adx_chunk_split(&c,used,&a,&b));
        int suffix_first=(int)random_n(2);
        if(suffix_first) OK(kosdec_adx_joint_unget(&j,&b));
        /* Publish in two pieces; second can start inside the extra region. */
        OK(kosdec_adx_chunk_split(&a,used/2,&part,&a));
        OK(kosdec_adx_joint_put(&j,&part)); OK(kosdec_adx_joint_put(&j,&a));
        if(!suffix_first) OK(kosdec_adx_joint_unget(&j,&b));
        if(line==DATA) {
            memmove(queue,queue+used,queued-used); queued-=used;
        } else {
            for(size_t i=0;i<used;++i) queue[queued++]=(uint8_t)generated++;
        }
        CHECK(storage[0]==0xa5 && storage[1+cap+extra]==0xa5);
        OK(kosdec_adx_joint_query(&j,&info));
        CHECK(!info.borrowed[0] && !info.borrowed[1]);
    }
    OK(kosdec_adx_joint_close(&j));
}
static void simultaneous(void) {
    uint8_t storage[24];
    struct kosdec_adx_joint j;
    struct kosdec_adx_chunk w,r,a,b;
    struct kosdec_adx_joint_info info;
    OK(kosdec_adx_joint_ring_init(&j,storage,16,8));
    OK(kosdec_adx_joint_get(&j,FREE,12,&w)); memset(w.data,7,w.bytes);
    OK(kosdec_adx_chunk_split(&w,6,&a,&b)); OK(kosdec_adx_joint_put(&j,&a));
    OK(kosdec_adx_joint_get(&j,DATA,6,&r));
    OK(kosdec_adx_joint_query(&j,&info));
    CHECK(info.borrowed[FREE]==6 && info.borrowed[DATA]==6 && info.available[FREE]==4);
    CHECK(!memcmp(r.data,"\7\7\7\7\7\7",6));
    OK(kosdec_adx_joint_put(&j,&r)); OK(kosdec_adx_joint_unget(&j,&b));
    OK(kosdec_adx_joint_get(&j,FREE,16,&w)); CHECK(w.bytes==16 && w.data==storage+6);
    for(size_t i=0;i<16;++i) w.data[i]=(uint8_t)i;
    OK(kosdec_adx_joint_put(&j,&w));
    OK(kosdec_adx_joint_get(&j,DATA,16,&r));
    for(size_t i=0;i<16;++i) CHECK(r.data[i]==i);
    OK(kosdec_adx_joint_put(&j,&r));
    OK(kosdec_adx_joint_reset(&j));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.available[FREE]==16);
    j.next_token=UINT64_MAX;
    struct kosdec_adx_joint before=j;
    CHECK(kosdec_adx_joint_get(&j,FREE,1,&w)==KOSDEC_ADX_JOINT_LIMIT); unchanged(&j,before);
    OK(kosdec_adx_joint_close(&j));
}
static void bad_args(void) {
    struct kosdec_adx_joint j={0}, before=j;
    uint8_t bytes[4]={0}; struct kosdec_adx_chunk c={0},a={0},b={0};
    CHECK(kosdec_adx_joint_memory_init(&j,bytes,0)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_ring_init(&j,bytes,4,5)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_ring_init(&j,bytes,SIZE_MAX,1)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_memory_init(&j,NULL,4)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    unchanged(&j,before);
    CHECK(kosdec_adx_joint_get(NULL,DATA,1,&c)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_put(NULL,&c)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_unget(NULL,NULL)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_chunk_split(NULL,1,&a,&b)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_chunk_split(&c,1,&a,&a)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    c.bytes=1; CHECK(kosdec_adx_chunk_split(&c,1,&a,&b)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    OK(kosdec_adx_joint_memory_init(&j,bytes,4)); before=j;
    CHECK(kosdec_adx_joint_get(&j,(enum kosdec_adx_joint_line)2,1,&c)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_get(&j,DATA,1,NULL)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_put(&j,NULL)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_query(&j,NULL)==KOSDEC_ADX_JOINT_BAD_ARGUMENT); unchanged(&j,before);
    OK(kosdec_adx_joint_get(&j,DATA,4,&c));
    OK(kosdec_adx_chunk_split(&c,SIZE_MAX,&c,&b)); CHECK(c.bytes==4 && !b.bytes);
    OK(kosdec_adx_chunk_split(&c,0,&a,&c)); CHECK(!a.bytes && c.bytes==4);
    OK(kosdec_adx_joint_put(&j,&c)); OK(kosdec_adx_joint_close(&j));
}
int main(void) {
    bad_args(); memory(); simultaneous();
    for(unsigned cap=1;cap<=24;++cap)
        for(unsigned extra=0;extra<=cap;++extra) ring_model(cap,extra);
    printf("joint: %u checks passed\n",checks);
    return 0;
}
