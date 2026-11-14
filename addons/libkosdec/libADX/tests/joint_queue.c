#include <kosdec/adx_joint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"joint_queue:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
#define DATA KOSDEC_ADX_JOINT_DATA
#define FREE KOSDEC_ADX_JOINT_FREE
static void limits(void) {
    struct kosdec_adx_joint j, before;
    struct kosdec_adx_joint_node nodes[2], saved[2];
    struct kosdec_adx_chunk c,a,b;
    struct kosdec_adx_joint_info info;
    uint8_t bytes[16]={0};
    OK(kosdec_adx_joint_queue_init(&j,nodes,1,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,bytes,8));
    before=j; memcpy(saved,nodes,sizeof(nodes));
    CHECK(kosdec_adx_joint_get(&j,DATA,4,&c)==KOSDEC_ADX_JOINT_LIMIT);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(nodes,saved,sizeof(nodes)));
    OK(kosdec_adx_joint_get(&j,DATA,8,&c));
    OK(kosdec_adx_chunk_split(&c,4,&a,&b));
    before=j; memcpy(saved,nodes,sizeof(nodes));
    CHECK(kosdec_adx_joint_move(&j,FREE,&a)==KOSDEC_ADX_JOINT_LIMIT);
    CHECK(kosdec_adx_joint_unget(&j,&b)==KOSDEC_ADX_JOINT_LIMIT);
    CHECK(kosdec_adx_joint_seed(&j,FREE,bytes+8,8)==KOSDEC_ADX_JOINT_LIMIT);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(nodes,saved,sizeof(nodes)));
    OK(kosdec_adx_joint_unget(&j,&c));
    OK(kosdec_adx_joint_close(&j));

    OK(kosdec_adx_joint_queue_init(&j,nodes,2,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,bytes,8));
    OK(kosdec_adx_joint_get(&j,DATA,8,&c));
    OK(kosdec_adx_joint_seed(&j,FREE,bytes+8,8));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(!info.spare_nodes);
    before=j; memcpy(saved,nodes,sizeof(nodes));
    CHECK(kosdec_adx_joint_seed(&j,FREE,bytes+2,1)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_seed(&j,FREE,bytes+10,1)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_seed(&j,FREE,&j,1)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_seed(&j,FREE,nodes,1)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_seed(&j,FREE,bytes,SIZE_MAX)==KOSDEC_ADX_JOINT_LIMIT);
    CHECK(kosdec_adx_joint_reset(&j)==KOSDEC_ADX_JOINT_BUSY);
    CHECK(kosdec_adx_joint_close(&j)==KOSDEC_ADX_JOINT_BUSY);
    CHECK(!memcmp(&j,&before,sizeof(j)) && !memcmp(nodes,saved,sizeof(nodes)));
    OK(kosdec_adx_joint_unget(&j,&c)); /* Reserved node survives pool exhaustion. */
    OK(kosdec_adx_joint_reset(&j));
    OK(kosdec_adx_joint_query(&j,&info));
    CHECK(info.capacity==0 && info.spare_nodes==2 && !info.chunks[DATA]);
    OK(kosdec_adx_joint_seed(&j,DATA,bytes,8));
    OK(kosdec_adx_joint_get(&j,DATA,4,&c));
    OK(kosdec_adx_chunk_split(&c,2,&a,&b));
    OK(kosdec_adx_joint_unget(&j,&b)); /* Merge suffix with untouched head. */
    OK(kosdec_adx_joint_put(&j,&a));
    OK(kosdec_adx_joint_query(&j,&info));
    CHECK(info.available[FREE]==2 && info.available[DATA]==6 && !info.spare_nodes);
    OK(kosdec_adx_joint_get(&j,FREE,2,&a));
    OK(kosdec_adx_joint_move(&j,DATA,&a)); /* Tail insertion is not prepend. */
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.chunks[DATA]==2);
    OK(kosdec_adx_joint_close(&j));
}
struct span { unsigned offset, bytes; };
struct line { struct span span[64]; unsigned count; };
static void append(struct line *line, struct span s, int join) {
    if(!s.bytes) return;
    if(join && line->count && line->span[line->count-1].offset+
       line->span[line->count-1].bytes==s.offset)
        line->span[line->count-1].bytes+=s.bytes;
    else { CHECK(line->count<64); line->span[line->count++]=s; }
}
static void prepend(struct line *line, struct span s, int join) {
    if(!s.bytes) return;
    if(join && line->count && s.offset+s.bytes==line->span[0].offset) {
        line->span[0].offset=s.offset; line->span[0].bytes+=s.bytes;
    } else {
        CHECK(line->count<64);
        memmove(line->span+1,line->span,line->count*sizeof(*line->span));
        line->span[0]=s; ++line->count;
    }
}
static uint32_t random_state=121;
static unsigned rnd(unsigned n) {
    random_state=random_state*UINT32_C(1664525)+UINT32_C(1013904223);
    return (random_state>>8)%n;
}
static void model(int join) {
    struct kosdec_adx_joint j;
    struct kosdec_adx_joint_node nodes[128];
    struct line lines[4]={0};
    uint8_t bytes[64];
    for(unsigned i=0;i<64;++i) bytes[i]=(uint8_t)(i+1);
    OK(kosdec_adx_joint_queue_init(&j,nodes,128,
        join ? KOSDEC_ADX_JOINT_JOIN : KOSDEC_ADX_JOINT_SEPARATE));
    for(unsigned i=0;i<4;++i) {
        OK(kosdec_adx_joint_seed(&j,(enum kosdec_adx_joint_line)i,bytes+16*i,16));
        append(&lines[i],(struct span){16*i,16},join);
    }
    for(unsigned step=0;step<4000;++step) {
        unsigned origin=rnd(4),dest=rnd(4),request=rnd(80);
        struct line *line=&lines[origin];
        struct kosdec_adx_chunk c,a,b;
        unsigned n=line->count ? line->span[0].bytes : 0;
        if(n>request) n=join ? request : 0;
        OK(kosdec_adx_joint_get(&j,(enum kosdec_adx_joint_line)origin,request,&c));
        CHECK(c.bytes==n);
        if(n) {
            struct span old=line->span[0];
            CHECK(c.data==bytes+old.offset);
            for(unsigned i=0;i<n;++i) CHECK(c.data[i]==old.offset+i+1);
            if(n==old.bytes) {
                --line->count; memmove(line->span,line->span+1,line->count*sizeof(*line->span));
            } else { line->span[0].offset+=n; line->span[0].bytes-=n; }
            unsigned used=rnd(n+1),suffix_first=rnd(2);
            OK(kosdec_adx_chunk_split(&c,used,&a,&b));
            struct span prefix={old.offset,used},suffix={old.offset+used,n-used};
            if(suffix_first) {
                OK(kosdec_adx_joint_unget(&j,&b)); prepend(line,suffix,join);
            }
            OK(kosdec_adx_joint_move(&j,(enum kosdec_adx_joint_line)dest,&a));
            append(&lines[dest],prefix,join);
            if(!suffix_first) {
                OK(kosdec_adx_joint_unget(&j,&b)); prepend(line,suffix,join);
            }
        }
        struct kosdec_adx_joint_info info;
        OK(kosdec_adx_joint_query(&j,&info));
        size_t total=0,chunks=0;
        for(unsigned l=0;l<4;++l) {
            unsigned expected=0;
            for(unsigned k=0;k<lines[l].count;++k) expected+=lines[l].span[k].bytes;
            CHECK(info.available[l]==expected && info.chunks[l]==lines[l].count);
            CHECK(!info.borrowed[l]);
            CHECK(info.contiguous[l]==(lines[l].count ? lines[l].span[0].bytes : 0));
            total+=expected; chunks+=lines[l].count;
        }
        CHECK(total==64 && info.capacity==64 && chunks+info.spare_nodes==128);
    }
    OK(kosdec_adx_joint_close(&j));
}
static void modes(void) {
    struct kosdec_adx_joint j, before;
    struct kosdec_adx_joint_node nodes[4];
    struct kosdec_adx_chunk c;
    struct kosdec_adx_joint_info info;
    uint8_t data[4]={1,2,3,4};
    OK(kosdec_adx_joint_queue_init(&j,nodes,4,KOSDEC_ADX_JOINT_SEPARATE));
    OK(kosdec_adx_joint_seed(&j,DATA,data,2));
    OK(kosdec_adx_joint_seed(&j,DATA,data+2,2));
    OK(kosdec_adx_joint_get(&j,DATA,1,&c)); CHECK(!c.bytes);
    OK(kosdec_adx_joint_get(&j,DATA,3,&c)); CHECK(c.bytes==2);
    OK(kosdec_adx_joint_move(&j,KOSDEC_ADX_JOINT_AUX2,&c));
    OK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_AUX2,2,&c));
    OK(kosdec_adx_joint_put(&j,&c));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.available[KOSDEC_ADX_JOINT_AUX3]==2);
    before=j;
    CHECK(kosdec_adx_joint_seed(&j,(enum kosdec_adx_joint_line)4,data,0)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_move(&j,(enum kosdec_adx_joint_line)4,&c)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(!memcmp(&j,&before,sizeof(j)));
    OK(kosdec_adx_joint_close(&j));
    before=j;
    CHECK(kosdec_adx_joint_queue_init(&j,nodes,0,KOSDEC_ADX_JOINT_JOIN)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(kosdec_adx_joint_queue_init(&j,nodes,4,(enum kosdec_adx_joint_mode)2)==KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    CHECK(!memcmp(&j,&before,sizeof(j)));
    OK(kosdec_adx_joint_queue_init(&j,nodes,4,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,data,2));
    OK(kosdec_adx_joint_seed(&j,DATA,data+2,2));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.chunks[DATA]==1 && info.spare_nodes==3);
    OK(kosdec_adx_joint_get(&j,DATA,3,&c)); CHECK(c.bytes==3);
    OK(kosdec_adx_joint_unget(&j,&c));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.chunks[DATA]==1 && info.spare_nodes==3);
    OK(kosdec_adx_joint_close(&j));
}
int main(void) {
    limits(); modes(); model(0); model(1);
    printf("joint_queue: %u checks passed\n",checks);
    return 0;
}
