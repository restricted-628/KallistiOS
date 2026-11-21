#include <kosdec/adx_loader.h>
#include <kosdec/adx_joint_reader.h>
#include <kosdec/adx_service.h>
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
    if(n>s->fragment)n=s->fragment;
    for(size_t i=0;i<n;++i)dst[i]=sample(s,off+(unsigned)i);
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
static void lsc_flow(void) {
    struct fixture f;
    struct source a = {.bytes = 4096, .fragment = 512};
    struct source b = {.bytes = 2049, .fragment = 512};
    struct kosdec_adx_loader_buffer_info buffer;
    struct kosdec_adx_chunk loan;
    uint64_t first, next;

    init(&f, 0, 8192);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 2048) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader, "first", read_at, &a, a.bytes, 0, 2, &first) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader, "next", read_at, &b, b.bytes, 0, 2, &next) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && a.calls == 1);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 0) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 8193) == KOSDEC_ADX_LOADER_BAD_ARGUMENT);
    CHECK(info(&f).flow_limit == 0);
    CHECK(kosdec_adx_loader_get_buffer_info(&f.loader, 1, 44100, &buffer) && buffer.reload_bytes == 2048);
    CHECK(kosdec_adx_loader_pause(&f.loader, true) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && a.calls == 1);
    CHECK(kosdec_adx_loader_pause(&f.loader, false) == KOSDEC_ADX_LOADER_OK);
    for(unsigned i = 1; i < 8; ++i)
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && a.calls == i + 1);
    CHECK(entry(&f, first).state == KOSDEC_ADX_LOADER_END && info(&f).pending == 1);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_NEED_OUTPUT && b.calls == 0);
    CHECK(entry(&f, next).state == KOSDEC_ADX_LOADER_READING);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 4096) == KOSDEC_ADX_LOADER_OK);
    CHECK(info(&f).flow_limit == 4096);
    CHECK(kosdec_adx_joint_get(&f.joint, KOSDEC_ADX_JOINT_DATA, 4096, &loan) == KOSDEC_ADX_JOINT_OK && loan.bytes == 4096);
    CHECK(kosdec_adx_joint_put(&f.joint, &loan) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_NEED_OUTPUT && b.calls == 0);
    CHECK(kosdec_adx_loader_get_buffer_info(&f.loader, 1, 44100, &buffer) &&
          buffer.reload_bytes == 0 && buffer.queued_bytes == 0);
    CHECK(kosdec_adx_loader_set_flow(&f.loader, 0) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 2048) == KOSDEC_ADX_LOADER_BUSY);
    for(unsigned i = 0; i < 5; ++i)
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(entry(&f, next).published_bytes == 2049 && info(&f).state == KOSDEC_ADX_LOADER_WAIT);
    CHECK(kosdec_adx_loader_close(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint) == KOSDEC_ADX_JOINT_OK);

    init(&f, 0, 8192);
    CHECK(kosdec_adx_joint_get(&f.joint, KOSDEC_ADX_JOINT_FREE, 2048, &loan) == KOSDEC_ADX_JOINT_OK && loan.bytes == 2048);
    CHECK(kosdec_adx_joint_put(&f.joint, &loan) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 2048) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_repeat(&f.loader, true) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader, "held", read_at, &a, a.bytes, 0, 2, &first) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_NEED_OUTPUT && a.calls == 8);
    CHECK(kosdec_adx_joint_get(&f.joint, KOSDEC_ADX_JOINT_DATA, 2048, &loan) == KOSDEC_ADX_JOINT_OK && loan.bytes == 2048);
    for(unsigned i = 0; i < 8; ++i)
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && a.calls == 9 + i);
    CHECK(entry(&f, first).completed_passes == 1 && info(&f).pending == 1);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 0) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_NEED_OUTPUT && a.calls == 16);
    CHECK(kosdec_adx_loader_stop(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(entry(&f, first).state == KOSDEC_ADX_LOADER_CANCELLED && info(&f).flow_limit == 0);
    CHECK(kosdec_adx_joint_put(&f.joint, &loan) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_close(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint) == KOSDEC_ADX_JOINT_OK);

    const unsigned capacity = 514 * 2048;
    uint8_t *ring = malloc(capacity);
    CHECK(ring != NULL);
    CHECK(kosdec_adx_joint_ring_init(&f.joint, ring, capacity, 0) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_init(&f.loader, &f.joint) == KOSDEC_ADX_LOADER_OK);
    a.bytes = capacity;
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 1) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader, "batch-cap", read_at, &a, a.bytes, 0, 514, &first) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && a.calls == 17);
    struct kosdec_adx_request_info request;
    CHECK(kosdec_adx_request_get_info(&f.loader.request, &request) && request.requested == 512);
    CHECK(kosdec_adx_loader_close(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint) == KOSDEC_ADX_JOINT_OK);
    free(ring);
}

static void lsc_wrap(void) {
    struct fixture f;
    struct kosdec_adx_chunk loan;
    struct source source = {.bytes = 2049, .fragment = 512, .salt = 23};
    uint8_t output[2049];
    uint64_t id;

    init(&f, 0, 4096);
    CHECK(kosdec_adx_joint_get(&f.joint, KOSDEC_ADX_JOINT_FREE, 3584, &loan) == KOSDEC_ADX_JOINT_OK && loan.bytes == 3584);
    CHECK(kosdec_adx_joint_put(&f.joint, &loan) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_get(&f.joint, KOSDEC_ADX_JOINT_DATA, 3584, &loan) == KOSDEC_ADX_JOINT_OK && loan.bytes == 3584);
    CHECK(kosdec_adx_joint_put(&f.joint, &loan) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_set_lsc_flow(&f.loader, 4096) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&f.loader, "wrapped", read_at, &source,
                                   source.bytes, 0, 2, &id) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader) == KOSDEC_ADX_LOADER_OK);
    for(unsigned i = 0; i < 5; ++i)
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK && source.calls == i + 1);
    CHECK(entry(&f, id).state == KOSDEC_ADX_LOADER_END);
    CHECK(drain(&f, output, sizeof(output)) == 512);
    CHECK(drain(&f, output + 512, sizeof(output) - 512) == sizeof(output) - 512);
    for(unsigned i = 0; i < sizeof(output); ++i) CHECK(output[i] == sample(&source, i));
    CHECK(kosdec_adx_loader_close(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint) == KOSDEC_ADX_JOINT_OK);
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
static void linked_loader_faults(bool jobs) {
    for(unsigned mode=0;mode<3;++mode) {
        struct fixture f;
        CHECK(kosdec_adx_joint_ring_init(&f.joint,f.buffer,4096,64)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_loader_init(&f.loader,&f.joint)==KOSDEC_ADX_LOADER_OK);
        struct source input={.bytes=8000,.fragment=512,.fail=1,.mode=mode};
        struct kosdec_adx_joint_reader reader;
        int16_t output[32], work[64];
        uint64_t id;
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"failure",read_at,&input,input.bytes,
                                        0,4,&id)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_joint_reader_init(&reader,&f.joint)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_joint_reader_enable_linked(&reader)==KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_joint_reader_set_loader(&reader,&f.loader,true)==KOSDEC_ADX_DONE);
        if(jobs)
            CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,work,64,32,31,false)==KOSDEC_ADX_DONE);
        enum kosdec_adx_status expected=mode ? KOSDEC_ADX_IO_ERROR : KOSDEC_ADX_TRUNCATED;
        struct kosdec_adx_joint_read_result result=kosdec_adx_joint_reader_read(&reader,output,32);
        CHECK(result.status==expected && !result.frames);
        CHECK(kosdec_adx_joint_reader_read(&reader,output,32).status==expected && input.calls==1);
        CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_joint_close(&f.joint)==KOSDEC_ADX_JOINT_OK);
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
    if(n>7)n=7;
    memcpy(dst,s->bytes+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
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
static void repeat_full_queue(void) {
    struct fixture f;
    struct source input = {.bytes=1,.fragment=1};
    uint64_t ids[16], current;
    init(&f,0,32);
    for(unsigned i=0;i<16;++i)
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"full",read_at,&input,1,0,1,&ids[i]) ==
              KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_repeat(&f.loader,true) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader) == KOSDEC_ADX_LOADER_OK && input.calls == 0);
    for(unsigned i=0;i<20;++i) {
        unsigned index=i%16;
        CHECK(kosdec_adx_loader_get_id(&f.loader,0,&current) && current==ids[index]);
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK);
        CHECK(info(&f).pending == 16 && input.calls == i+1);
        struct kosdec_adx_loader_source_info result=entry(&f,ids[index]);
        CHECK(result.state == KOSDEC_ADX_LOADER_QUEUED && result.completed_passes == i/16+1);
        CHECK(!result.read_sectors && !result.published_bytes);
        CHECK(kosdec_adx_loader_get_id(&f.loader,15,&current) && current==ids[index]);
    }
    CHECK(kosdec_adx_loader_set_repeat(&f.loader,false) == KOSDEC_ADX_LOADER_OK);
    uint8_t discarded[32];
    CHECK(drain(&f,discarded,sizeof(discarded)) == 20);
    for(unsigned i=0;i<16;++i)
        CHECK(kosdec_adx_loader_step(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(info(&f).state == KOSDEC_ADX_LOADER_WAIT && !info(&f).pending && input.calls == 36);
    for(unsigned i=0;i<16;++i)
        CHECK(entry(&f,ids[i]).state == KOSDEC_ADX_LOADER_END &&
              entry(&f,ids[i]).completed_passes == (i<4 ? 3u : 2u));
    CHECK(kosdec_adx_loader_close(&f.loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint) == KOSDEC_ADX_JOINT_OK);
}

struct record {
    uint8_t data[288+3*18+18];
    unsigned bytes, calls;
};
static void make_record(struct record *r, unsigned frames, unsigned salt) {
    *r=(struct record){0};
    CHECK(kosdec_adx_make_sfa_header(r->data,288,1,44100,frames)==KOSDEC_ADX_DONE);
    unsigned groups=(frames+31)/32;
    unsigned footer=288+groups*18;
    r->bytes=footer+18;
    for(unsigned i=0;i<groups;++i) {
        r->data[288+i*18+1]=(uint8_t)(salt+2*i);
        memset(r->data+290+i*18,0x12+salt,16);
    }
    r->data[footer]=0x80;
    r->data[footer+1]=1;
    r->data[footer+3]=14;
}
static struct kosdec_adx_input_result record_read(void *p,uint32_t off,uint8_t *dst,size_t n) {
    struct record *r=p;
    CHECK(n && n<=512 && off<r->bytes && n<=r->bytes-off);
    if(++r->calls%5==0) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(n>7) n=7;
    memcpy(dst,r->data+off,n);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,n};
}
static void render_record(struct record *r, int16_t *pcm, size_t frames) {
    struct kosdec_adx_decoder d;
    kosdec_adx_init(&d);
    size_t bytes=0,samples=0;
    for(unsigned i=0;i<12;++i) {
        struct kosdec_adx_result step=kosdec_adx_decode(&d,r->data+bytes,r->bytes-bytes,
                                                       pcm+samples,32,true);
        bytes+=step.consumed;
        samples+=step.frames;
        if(step.status==KOSDEC_ADX_DONE) {
            CHECK(bytes==r->bytes && samples==frames);
            return;
        }
        CHECK(step.status<KOSDEC_ADX_DONE && samples<=frames);
    }
    CHECK(false);
}
static void linked_loader(bool jobs, bool release_record, bool restore_link) {
    struct fixture f={0};
    struct record sources[2];
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_service service;
    int16_t work[64],fifo[64],first[65],second[33],played[196];
    uint64_t ids[2];
    make_record(&sources[0],65,1);
    make_record(&sources[1],33,3);
    render_record(&sources[0],first,65);
    render_record(&sources[1],second,33);
    CHECK(kosdec_adx_joint_ring_init(&f.joint,f.buffer,4096,64)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_init(&f.loader,&f.joint)==KOSDEC_ADX_LOADER_OK);
    for(unsigned i=0;i<2;++i)
        CHECK(kosdec_adx_loader_enqueue(&f.loader,"linked",record_read,&sources[i],
                                       sources[i].bytes,0,1,&ids[i])==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_set_repeat(&f.loader,true)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_reader_init(&reader,&f.joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_enable_linked(&reader)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_loader(&reader,&f.loader,true)==KOSDEC_ADX_DONE);
    if(jobs)
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,work,64,32,31,false)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_service_init_joint(&service,&reader,fifo,64,32)==KOSDEC_ADX_MORE);
    uint32_t pending;
    bool linked;
    CHECK(kosdec_adx_service_get_queue_count(&service,&pending) && pending==2);
    CHECK(kosdec_adx_service_get_linked(&service,&linked) && linked);
    struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&service,&receipt) && receipt==KOSDEC_ADX_MORE);
    bool retry_while_full=false;
    for(unsigned i=0;!entry(&f,ids[1]).completed_passes;++i) {
        CHECK(i<300);
        unsigned before=sources[0].calls+sources[1].calls;
        enum kosdec_adx_status status=kosdec_adx_service_step(&service).status;
        CHECK(status<KOSDEC_ADX_DONE);
        if(status==KOSDEC_ADX_NEED_INPUT && service.pipe.write_cursor==32)
            retry_while_full=true;
        CHECK(sources[0].calls+sources[1].calls<=before+1);
    }
    CHECK(service.pipe.write_cursor==32 && !service.pipe.read_cursor);
    CHECK(retry_while_full);
    CHECK(reader.decoder.frames_done==32 && !reader.linked.completed_records);
    CHECK(kosdec_adx_service_get_queue_count(&service,&pending) && pending==2);
    if(restore_link) {
        command.kind=KOSDEC_ADX_SET_LINK_FOLLOW;
        for(unsigned enabled=0;enabled<2;++enabled) {
            command.enabled=enabled!=0;
            CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
            CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_MORE);
            CHECK(kosdec_adx_service_take(&service,&receipt) && receipt==KOSDEC_ADX_MORE);
            CHECK(kosdec_adx_service_get_linked(&service,&linked) && linked==(enabled!=0));
        }
    }
    command.kind=release_record ? KOSDEC_ADX_RELEASE_LINK : KOSDEC_ADX_SEQUENCE_REPEAT;
    command.enabled=false;
    CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&service,&receipt) && receipt==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_get_linked(&service,&linked) && linked==!release_record);
    size_t frames=0;
    size_t expected=release_record ? 65 : 196;
    for(unsigned n=0;n<1000;++n) {
        struct kosdec_adx_pipe_result p=kosdec_adx_pipe_read(&service.pipe,played+frames,expected-frames);
        CHECK(p.frames<=expected-frames);
        frames+=p.frames;
        if(p.producer==KOSDEC_ADX_DONE && !p.queued_frames) break;
        CHECK(kosdec_adx_service_step(&service).status<=KOSDEC_ADX_DONE);
    }
    CHECK(frames==expected && reader.terminal==KOSDEC_ADX_DONE);
    CHECK(!memcmp(played,first,sizeof(first)));
    if(!release_record) {
        CHECK(!memcmp(played+65,second,sizeof(second)));
        CHECK(!memcmp(played+98,first,sizeof(first)));
        CHECK(!memcmp(played+163,second,sizeof(second)));
        CHECK(reader.linked.completed_records==4 && info(&f).state==KOSDEC_ADX_LOADER_WAIT);
        CHECK(kosdec_adx_service_get_queue_count(&service,&pending) && pending==0);
        CHECK(entry(&f,ids[0]).completed_passes==2 && entry(&f,ids[1]).completed_passes==2);
    }
    else CHECK(reader.linked.completed_records==1 && f.loader.repeat);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint)==KOSDEC_ADX_JOINT_OK);
}
static void linked_input_error(bool jobs, bool format_change) {
    struct record a,b;
    uint8_t data[sizeof(a.data)+sizeof(b.data)];
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    int16_t scratch[128], out[128];
    make_record(&a,33,1);
    make_record(&b,33,3);
    if(format_change)
        CHECK(kosdec_adx_make_sfa_header(b.data,288,1,32000,33)==KOSDEC_ADX_DONE);
    else a.data[a.bytes-1]=1;
    memcpy(data,a.data,a.bytes);
    memcpy(data+a.bytes,b.data,b.bytes);
    CHECK(kosdec_adx_joint_memory_init(&joint,data,a.bytes+b.bytes)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_enable_linked(&reader)==KOSDEC_ADX_DONE);
    if(jobs)
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,scratch,128,64,31,false)==KOSDEC_ADX_DONE);
    size_t frames=0;
    enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<16 && status<KOSDEC_ADX_DONE;++i) {
        struct kosdec_adx_joint_read_result step=kosdec_adx_joint_reader_read(&reader,out,64);
        status=step.status;
        frames+=step.frames;
    }
    CHECK(status==(format_change ? KOSDEC_ADX_UNSUPPORTED : KOSDEC_ADX_INVALID));
    CHECK(frames==33 && reader.linked.completed_records==format_change);
    CHECK(kosdec_adx_joint_reader_read(&reader,out,64).status==status);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
}
static void publish_bytes(struct kosdec_adx_joint *joint, const uint8_t *input, size_t len) {
    while(len) {
        struct kosdec_adx_chunk chunk;
        CHECK(kosdec_adx_joint_get(joint,KOSDEC_ADX_JOINT_FREE,len,&chunk)==KOSDEC_ADX_JOINT_OK);
        CHECK(chunk.bytes && chunk.bytes<=len);
        memcpy(chunk.data,input,chunk.bytes);
        CHECK(kosdec_adx_joint_put(joint,&chunk)==KOSDEC_ADX_JOINT_OK);
        input+=chunk.bytes;
        len-=chunk.bytes;
    }
}
static void buffered_scan(unsigned scenario, bool jobs) {
    enum { CAP=2048 };
    enum { MIXED, LONG_PAD, WRAPPED, ODD_PAD, LATE, INCOMPLETE, CANCEL };
    struct record a,b;
    uint8_t ring[CAP+64], pad[CAP];
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    int16_t scratch[64], out[64], expected[50], actual[50];
    make_record(&a,33,1);
    make_record(&b,17,3);
    render_record(&a,expected,33);
    render_record(&b,expected+33,17);
    memset(pad,0xff,sizeof(pad));
    pad[1]=1;
    CHECK(kosdec_adx_joint_ring_init(&joint,ring,CAP,jobs?64:0)==KOSDEC_ADX_JOINT_OK);
    if(scenario==WRAPPED || scenario==CANCEL) {
        size_t shift=CAP-1-a.bytes;
        publish_bytes(&joint,pad,shift);
        struct kosdec_adx_chunk chunk;
        CHECK(kosdec_adx_joint_get(&joint,KOSDEC_ADX_JOINT_DATA,shift,&chunk)==KOSDEC_ADX_JOINT_OK);
        CHECK(chunk.bytes==shift && kosdec_adx_joint_put(&joint,&chunk)==KOSDEC_ADX_JOINT_OK);
    }
    unsigned padding=scenario==MIXED ? 6 : scenario==LONG_PAD ? 900 :
        scenario==WRAPPED || scenario==ODD_PAD || scenario==CANCEL ? 1 : 0;
    publish_bytes(&joint,a.data,a.bytes);
    publish_bytes(&joint,pad,padding);
    size_t before=scenario==LATE ? 0 : scenario==INCOMPLETE ? 2 : b.bytes;
    publish_bytes(&joint,b.data,before);
    if(before==b.bytes) CHECK(kosdec_adx_joint_finish(&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_init(&reader,&joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_enable_linked(&reader)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_enable_link_scan(&reader)==KOSDEC_ADX_DONE);
    if(jobs)
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,scratch,64,32,31,false)==KOSDEC_ADX_DONE);
    size_t frames=0;
    bool delayed=false, released=false;
    for(unsigned i=0;i<24 && !reader.terminal;++i) {
        struct kosdec_adx_joint_read_result step=kosdec_adx_joint_reader_read(&reader,out,32);
        CHECK(step.status<=KOSDEC_ADX_DONE && step.frames<=50-frames);
        memcpy(actual+frames,out,step.frames*sizeof(*out));
        frames+=step.frames;
        if(scenario==CANCEL && reader.linked.scan_active) {
            CHECK(reader.held.bytes==1 && reader.linked.next.bytes==b.bytes);
            kosdec_adx_joint_reader_cancel(&reader);
            CHECK(kosdec_adx_joint_reader_read(&reader,out,32).status==KOSDEC_ADX_CANCELLED);
            CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
            struct kosdec_adx_joint_info remaining;
            CHECK(kosdec_adx_joint_query(&joint,&remaining)==KOSDEC_ADX_JOINT_OK);
            CHECK(!remaining.borrowed[KOSDEC_ADX_JOINT_DATA] &&
                  remaining.available[KOSDEC_ADX_JOINT_DATA]==padding+b.bytes);
            CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
            return;
        }
        if(before<b.bytes && reader.linked.scan_active && !delayed) {
            delayed=true;
            publish_bytes(&joint,b.data+before,b.bytes-before);
            CHECK(kosdec_adx_joint_finish(&joint)==KOSDEC_ADX_JOINT_OK);
        }
        if(scenario==LONG_PAD && reader.linked.header_selected && !released) {
            released=true;
            CHECK(kosdec_adx_joint_reader_release_link(&reader)==KOSDEC_ADX_MORE);
        }
    }
    bool linked=scenario<=WRAPPED;
    CHECK(reader.terminal==KOSDEC_ADX_DONE && frames==(linked?50u:33u));
    CHECK(!memcmp(actual,expected,frames*sizeof(*actual)));
    CHECK(reader.linked.completed_records==(linked?2u:1u));
    CHECK(!reader.held.bytes && !reader.linked.next.bytes);
    CHECK(reader.consumed_bytes==a.bytes+(linked?padding+b.bytes:0));
    if(!linked) {
        struct kosdec_adx_joint_info remaining;
        CHECK(kosdec_adx_joint_query(&joint,&remaining)==KOSDEC_ADX_JOINT_OK);
        CHECK(remaining.available[KOSDEC_ADX_JOINT_DATA]==padding+b.bytes && !reader.linked.follow);
    }
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_close(&joint)==KOSDEC_ADX_JOINT_OK);
}

static struct kosdec_adx_command_receipt submit(struct kosdec_adx_service *service,
                                               struct kosdec_adx_command *command) {
    CHECK(kosdec_adx_service_post(service,command)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(service).status==KOSDEC_ADX_MORE);
    struct kosdec_adx_command_receipt result;
    CHECK(kosdec_adx_service_take_receipt(service,&result));
    CHECK(!kosdec_adx_service_take_receipt(service,&result));
    return result;
}
static void live_registration(void) {
    struct fixture f;
    struct record sources[2];
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_service service;
    int16_t fifo[64], out[32];
    init(&f,0,1024);
    make_record(&sources[0],33,1);
    make_record(&sources[1],17,3);
    CHECK(kosdec_adx_loader_start(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_reader_init(&reader,&f.joint)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_reader_enable_linked(&reader)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_loader(&reader,&f.loader,false)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_service_init_joint(&service,&reader,fifo,64,32)==KOSDEC_ADX_MORE);
    struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    CHECK(submit(&service,&command).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_NEED_INPUT);
    size_t frames=0;
    for(unsigned i=0;i<2;++i) {
        command=(struct kosdec_adx_command){
            .kind=KOSDEC_ADX_LOADER_ENQUEUE,
            .source={record_read,&sources[i],sources[i].bytes,0},
            .label="live record"
        };
        CHECK(kosdec_adx_service_post(&service,&command)==KOSDEC_ADX_MORE);
        command.label[0]='X';
        CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_MORE);
        struct kosdec_adx_command_receipt receipt;
        CHECK(kosdec_adx_service_take_receipt(&service,&receipt));
        CHECK(receipt.status==KOSDEC_ADX_MORE && receipt.source_id==i+1);
        CHECK(!strcmp(entry(&f,receipt.source_id).name,"live record"));
        for(unsigned step=0;reader.linked.completed_records!=i+1;++step) {
            CHECK(step<200 && kosdec_adx_service_step(&service).status<KOSDEC_ADX_DONE);
            frames+=kosdec_adx_pipe_read(&service.pipe,out,32).frames;
        }
        CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_NEED_INPUT);
    }
    CHECK(frames==50);
    command.kind=KOSDEC_ADX_SEQUENCE_REPEAT;
    command.enabled=true;
    CHECK(submit(&service,&command).status==KOSDEC_ADX_MORE && f.loader.repeat);
    command.kind=KOSDEC_ADX_LOADER_FINISH;
    struct kosdec_adx_command_receipt receipt=submit(&service,&command);
    CHECK(receipt.status==KOSDEC_ADX_MORE && !receipt.source_id && !f.loader.repeat);
    command.kind=KOSDEC_ADX_LOADER_ENQUEUE;
    receipt=submit(&service,&command);
    CHECK(receipt.status==KOSDEC_ADX_BAD_ARGUMENT && !receipt.source_id);
    command.kind=KOSDEC_ADX_SEQUENCE_REPEAT;
    CHECK(submit(&service,&command).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_service_step(&service).status==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_close(&reader)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_close(&f.loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_joint_close(&f.joint)==KOSDEC_ADX_JOINT_OK);
}
int main(void) {
    concatenation(); queue(); flow_pause(); lsc_flow(); lsc_wrap(); faults(); limits(); live_decoder();
    repeat_full_queue();
    for(unsigned jobs=0;jobs<2;++jobs) {
        linked_loader_faults(jobs);
        linked_loader(jobs,false,true);
        for(unsigned release=0;release<2;++release) {
            linked_loader(jobs,release,false);
            linked_input_error(jobs,release);
        }
    }
    for(unsigned scenario=0;scenario<6;++scenario) {
        buffered_scan(scenario,false);
        if(scenario!=2) buffered_scan(scenario,true);
    }
    buffered_scan(6,false);
    live_registration();
    printf("LOADER: %u checks passed\n",checks);
    return 0;
}
