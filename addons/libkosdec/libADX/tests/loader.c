#include <kosdec/adx_loader.h>
#include <kosdec/adx_joint_reader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"loader:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct source {unsigned bytes,fragment,calls,wait,fail,mode,salt;};
static uint8_t sample(struct source *s,unsigned at){return (uint8_t)(at*17+at/257+s->salt);}
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t n) {
    struct source *s=ctx;++s->calls;CHECK(n && n<=512 && off<s->bytes && n<=s->bytes-off);
    if(s->calls==s->fail)return (struct kosdec_adx_input_result){s->mode==0?KOSDEC_ADX_INPUT_END:s->mode==1?KOSDEC_ADX_INPUT_ERROR:KOSDEC_ADX_INPUT_DATA,s->mode==2?n+1:0};
    if(s->wait && s->calls%s->wait==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(n>s->fragment)n=s->fragment;for(size_t i=0;i<n;++i)dst[i]=sample(s,off+(unsigned)i);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
struct fixture {struct kosdec_adx_loader loader;struct kosdec_adx_joint joint;struct kosdec_adx_joint_node nodes[8];uint8_t buffer[8192];};
static void init(struct fixture *f,unsigned kind,unsigned capacity) {
    memset(f,0,sizeof(*f));
    if(!kind)CHECK(kosdec_adx_joint_ring_init(&f->joint,f->buffer,capacity,0)==KOSDEC_ADX_JOINT_OK);
    else {CHECK(kosdec_adx_joint_queue_init(&f->joint,f->nodes,8,kind==1?KOSDEC_ADX_JOINT_SEPARATE:KOSDEC_ADX_JOINT_JOIN)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_joint_seed(&f->joint,KOSDEC_ADX_JOINT_FREE,f->buffer,capacity/2)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_joint_seed(&f->joint,KOSDEC_ADX_JOINT_FREE,f->buffer+capacity/2,capacity-capacity/2)==KOSDEC_ADX_JOINT_OK);}
    CHECK(kosdec_adx_loader_init(&f->loader,&f->joint)==KOSDEC_ADX_LOADER_OK);
}
static struct kosdec_adx_loader_info info(struct fixture *f){struct kosdec_adx_loader_info r;CHECK(kosdec_adx_loader_get_info(&f->loader,&r));return r;}
static struct kosdec_adx_loader_source_info entry(struct fixture *f,uint64_t id){struct kosdec_adx_loader_source_info r;CHECK(kosdec_adx_loader_get_source(&f->loader,id,&r));return r;}
static size_t drain(struct fixture *f,uint8_t *dst,size_t room) {
    struct kosdec_adx_chunk c;CHECK(kosdec_adx_joint_get(&f->joint,KOSDEC_ADX_JOINT_DATA,room,&c)==KOSDEC_ADX_JOINT_OK);
    if(c.bytes){memcpy(dst,c.data,c.bytes);CHECK(kosdec_adx_joint_put(&f->joint,&c)==KOSDEC_ADX_JOINT_OK);}return c.bytes;
}
static void concatenation(void) {
    unsigned fragments[]={1,7,511,512};
    for(unsigned kind=0;kind<3;++kind)for(unsigned j=0;j<4;++j) {
        struct fixture f;init(&f,kind,1025);struct source a={3001,fragments[j],0,7,0,0,11},b={8000,fragments[j],0,5,0,0,97};
        uint64_t ids[3],id;char name[]="first";
        CHECK(kosdec_adx_loader_enqueue(&f.loader,name,read_at,&a,a.bytes,0,UINT32_MAX,&ids[0])==KOSDEC_ADX_LOADER_OK);name[0]='X';
        CHECK(!strcmp(entry(&f,ids[0]).name,"first"));
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"range",read_at,&b,b.bytes,1,2,&ids[1])==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"empty",read_at,&a,a.bytes,UINT32_MAX,UINT32_MAX,&ids[2])==KOSDEC_ADX_LOADER_OK);
        CHECK(entry(&f,ids[2]).sectors==0 && entry(&f,ids[2]).data_bytes==0);
        for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_loader_get_id(&f.loader,i,&id) && id==ids[i]);
        CHECK(!kosdec_adx_loader_get_id(&f.loader,3,&id));CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_BUSY);
        uint8_t out[8192];size_t used=0;
        for(unsigned i=0;i<30000 && info(&f).state!=KOSDEC_ADX_LOADER_WAIT;++i) {
            unsigned calls=a.calls+b.calls;enum kosdec_adx_loader_status s=kosdec_adx_loader_step(&f.loader);
            CHECK(s<=KOSDEC_ADX_LOADER_NEED_OUTPUT && a.calls+b.calls<=calls+1);
            used+=drain(&f,out+used,sizeof(out)-used);
        }
        CHECK(info(&f).state==KOSDEC_ADX_LOADER_WAIT && !info(&f).pending);
        used+=drain(&f,out+used,sizeof(out)-used);CHECK(used==7097);
        for(unsigned i=0;i<3001;++i)CHECK(out[i]==sample(&a,i));
        for(unsigned i=0;i<4096;++i)CHECK(out[3001+i]==sample(&b,2048+i));
        CHECK(entry(&f,ids[0]).read_sectors==2 && entry(&f,ids[0]).published_bytes==3001);
        CHECK(entry(&f,ids[1]).read_sectors==2 && entry(&f,ids[1]).state==KOSDEC_ADX_LOADER_END);
        CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_joint_close(&f.joint)==KOSDEC_ADX_JOINT_OK);
    }
}
static void queue(void) {
    struct fixture f;init(&f,0,257);struct source a={1,1,0,0,0,0,0};uint64_t ids[16],id=999;
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK && info(&f).state==KOSDEC_ADX_LOADER_WAIT);
    for(unsigned i=0;i<16;++i)CHECK(kosdec_adx_loader_enqueue(&f.loader,"",read_at,&a,0,0,0,&ids[i])==KOSDEC_ADX_LOADER_OK && ids[i]==i+1);
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"full",read_at,&a,0,0,0,&id)==KOSDEC_ADX_LOADER_LIMIT && id==999);
    for(unsigned i=0;i<16;++i){CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);CHECK(entry(&f,ids[i]).state==KOSDEC_ADX_LOADER_END && info(&f).pending==15-i);}
    CHECK(!a.calls);CHECK(kosdec_adx_loader_enqueue(&f.loader,"replacement",read_at,&a,1,0,1,&id)==KOSDEC_ADX_LOADER_OK && id==17);
    struct kosdec_adx_loader_source_info saved;CHECK(!kosdec_adx_loader_get_source(&f.loader,ids[0],&saved));
    CHECK(kosdec_adx_loader_stop(&f.loader)==KOSDEC_ADX_LOADER_OK && entry(&f,id).state==KOSDEC_ADX_LOADER_CANCELLED);
    char longname[41];memset(longname,'n',40);longname[40]=0;id=99;
    CHECK(kosdec_adx_loader_enqueue(&f.loader,longname,read_at,&a,1,0,1,&id)==KOSDEC_ADX_LOADER_BAD_ARGUMENT && id==99);
    f.loader.next_id=UINT64_MAX;CHECK(kosdec_adx_loader_enqueue(&f.loader,"last",read_at,&a,0,0,0,&id)==KOSDEC_ADX_LOADER_OK && id==UINT64_MAX);
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"overflow",read_at,&a,0,0,0,&id)==KOSDEC_ADX_LOADER_LIMIT);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    struct kosdec_adx_loader_info old={.pending=99};CHECK(!kosdec_adx_loader_get_info(&f.loader,&old) && old.pending==99);
}
static void flow_pause(void) {
    struct fixture f;init(&f,0,4096);struct source a={8000,512,0,0,0,0,0};uint64_t id;
    CHECK(info(&f).flow_limit==3276);CHECK(kosdec_adx_loader_set_flow(&f.loader,2048)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_flow(&f.loader,4097)==KOSDEC_ADX_LOADER_BAD_ARGUMENT && info(&f).flow_limit==2048);
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"flow",read_at,&a,8000,0,4,&id)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(a.calls==4 && entry(&f,id).read_sectors==1 && entry(&f,id).published_bytes==2048);
    CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && a.calls==4);
    uint8_t out[64];CHECK(drain(&f,out,17)==17);CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK && a.calls==5);
    CHECK(kosdec_adx_loader_pause(&f.loader,true)==KOSDEC_ADX_LOADER_OK);
    for(unsigned i=0;i<10;++i)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK && a.calls==5);
    CHECK(kosdec_adx_loader_pause(&f.loader,false)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_stop(&f.loader)==KOSDEC_ADX_LOADER_OK);
    struct kosdec_adx_joint_info joint;CHECK(kosdec_adx_joint_query(&f.joint,&joint)==KOSDEC_ADX_JOINT_OK && !joint.borrowed[0] && joint.available[1]==2543);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    init(&f,1,1025);a.calls=0;a.fragment=7;
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"held",read_at,&a,8000,0,4,&id)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_query(&f.joint,&joint)==KOSDEC_ADX_JOINT_OK && joint.borrowed[0]==512 && !joint.available[1]);
    CHECK(kosdec_adx_loader_pause(&f.loader,true)==KOSDEC_ADX_LOADER_OK);CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK && a.calls==1);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_query(&f.joint,&joint)==KOSDEC_ADX_JOINT_OK && !joint.borrowed[0] && joint.available[0]==1025 && !joint.available[1]);
}
struct errors {unsigned count;uint64_t id;enum kosdec_adx_request_status status;};
static void error(void *ctx,uint64_t id,enum kosdec_adx_request_status status){struct errors *e=ctx;++e->count;e->id=id;e->status=status;}
static void faults(void) {
    for(unsigned mode=0;mode<3;++mode) {
        struct fixture f;init(&f,0,4096);struct source a={8000,512,0,0,3,mode,0};struct errors e={0};uint64_t id;
        CHECK(kosdec_adx_loader_set_error(&f.loader,error,&e)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"failure",read_at,&a,8000,0,4,&id)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
        for(unsigned i=0;i<2;++i)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);
        for(unsigned i=0;i<5;++i)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_IO_ERROR);
        CHECK(e.count==1 && e.id==id && a.calls==3 && e.status==(mode?KOSDEC_ADX_REQUEST_IO_ERROR:KOSDEC_ADX_REQUEST_TRUNCATED));
        CHECK(entry(&f,id).published_bytes==1024);CHECK(kosdec_adx_loader_stop(&f.loader)==KOSDEC_ADX_LOADER_OK && !info(&f).pending && info(&f).state==KOSDEC_ADX_LOADER_STOPPED);
        CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    }
}
static void limits(void) {
    struct fixture f;init(&f,0,4096);struct source a={UINT32_MAX,512,0,0,0,0,0};uint64_t id;
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"tail",read_at,&a,UINT32_MAX,UINT32_MAX/2048,UINT32_MAX,&id)==KOSDEC_ADX_LOADER_OK);
    CHECK(entry(&f,id).sectors==1 && entry(&f,id).data_bytes==2047);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    while(info(&f).pending)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);
    uint8_t pcm[4096];CHECK(drain(&f,pcm,sizeof(pcm))==2047);
    for(unsigned i=0;i<2047;++i)CHECK(pcm[i]==sample(&a,(UINT32_MAX/2048)*2048+i));
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    init(&f,0,4096);a.bytes=4096;a.calls=0;
    CHECK(kosdec_adx_loader_set_flow(&f.loader,512)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"borrow",read_at,&a,4096,0,2,&id)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    for(unsigned i=0;i<4;++i)CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK);
    struct kosdec_adx_chunk held;CHECK(kosdec_adx_joint_get(&f.joint,KOSDEC_ADX_JOINT_DATA,2048,&held)==KOSDEC_ADX_JOINT_OK && held.bytes==2048);
    CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && a.calls==4);
    CHECK(kosdec_adx_joint_put(&f.joint,&held)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_set_flow(&f.loader,0)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader)==KOSDEC_ADX_LOADER_OK && a.calls==5);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
}
struct adx_source {uint8_t bytes[3654];unsigned calls;};
static struct kosdec_adx_input_result adx_read(void *ctx,uint32_t off,uint8_t *dst,size_t n) {
    struct adx_source *s=ctx;++s->calls;CHECK(n && n<=512 && off<sizeof(s->bytes) && n<=sizeof(s->bytes)-off);
    if(s->calls%7==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(n>7)n=7;memcpy(dst,s->bytes+off,n);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void live_decoder(void) {
    struct fixture f;init(&f,0,41);struct adx_source src={0};uint8_t *p=src.bytes;
    p[0]=0x80;p[3]=32;p[4]=3;p[5]=18;p[6]=4;p[7]=1;p[10]=0xac;p[11]=0x44;
    p[14]=0x19;p[16]=1;p[17]=0xf4;p[18]=3;memcpy(p+30,"(c)CRI",6);
    p[3636]=0x80;p[3637]=1;p[3639]=14;
    uint64_t first,second;CHECK(kosdec_adx_loader_enqueue(&f.loader,"first sector",adx_read,&src,sizeof(src.bytes),0,1,&first)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader,"rest",adx_read,&src,sizeof(src.bytes),1,1,&second)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    struct kosdec_adx_joint_reader reader;CHECK(kosdec_adx_joint_reader_init(&reader,&f.joint)==KOSDEC_ADX_JOINT_OK);
    bool closed=false,done=false;unsigned frames=0;
    for(unsigned i=0;i<20000 && !done;++i) {
        if(!closed){unsigned before=src.calls;CHECK(kosdec_adx_loader_step(&f.loader)<=KOSDEC_ADX_LOADER_NEED_OUTPUT && src.calls<=before+1);
            if(!info(&f).pending){CHECK(entry(&f,first).published_bytes==2048 && entry(&f,second).published_bytes==1606);
                CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);CHECK(kosdec_adx_joint_finish(&f.joint)==KOSDEC_ADX_JOINT_OK);closed=true;}}
        int16_t out[64];struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,out,32);
        CHECK(r.status<=KOSDEC_ADX_DONE);for(size_t j=0;j<r.frames;++j)CHECK(!out[j]);frames+=(unsigned)r.frames;done=r.status==KOSDEC_ADX_DONE;
    }
    CHECK(done && closed && frames==6400 && reader.consumed_bytes==sizeof(src.bytes));
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);CHECK(kosdec_adx_joint_close(&f.joint)==KOSDEC_ADX_JOINT_OK);
}
int main(void){concatenation();queue();flow_pause();faults();limits();live_decoder();printf("LOADER: %u checks passed\n",checks);return 0;}
