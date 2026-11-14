#include <kosdec/adx_loader.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,calls;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"INPUT_POLICY_FAIL line=%u\n",(unsigned)__LINE__); exit(1); } } while(0)
struct fixture {
    struct kosdec_adx_joint joint;
    struct kosdec_adx_loader loader;
    struct kosdec_adx_joint_node nodes[16];
    uint8_t bytes[131073];
};
static void init(struct fixture *f,unsigned queue,size_t capacity) {
    memset(f,0,sizeof(*f));
    if(queue) {
        CHECK(kosdec_adx_joint_queue_init(&f->joint,f->nodes,16,KOSDEC_ADX_JOINT_JOIN)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_joint_seed(&f->joint,KOSDEC_ADX_JOINT_FREE,f->bytes,capacity)==KOSDEC_ADX_JOINT_OK);
    }
    else CHECK(kosdec_adx_joint_ring_init(&f->joint,f->bytes,capacity,0)==KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_loader_init(&f->loader,&f->joint)==KOSDEC_ADX_LOADER_OK);
}
static void close_fixture(struct fixture *f) {
    CHECK(kosdec_adx_loader_close(&f->loader)==KOSDEC_ADX_LOADER_OK);
    CHECK(!kosdec_adx_loader_get_input_joint(&f->loader));
    CHECK(kosdec_adx_joint_close(&f->joint)==KOSDEC_ADX_JOINT_OK);
}
static struct kosdec_adx_loader_buffer_info info(struct fixture *f,unsigned ch,unsigned rate) {
    struct kosdec_adx_loader_buffer_info i;
    CHECK(kosdec_adx_loader_get_buffer_info(&f->loader,ch,rate,&i));return i;
}
static void publish(struct fixture *f,size_t bytes) {
    struct kosdec_adx_chunk c;
    CHECK(kosdec_adx_joint_get(&f->joint,KOSDEC_ADX_JOINT_FREE,bytes,&c)==KOSDEC_ADX_JOINT_OK);
    CHECK(c.bytes==bytes);memset(c.data,0,bytes);
    CHECK(kosdec_adx_joint_put(&f->joint,&c)==KOSDEC_ADX_JOINT_OK);
}
static void controls(void) {
    struct fixture *f=malloc(sizeof(*f));CHECK(f);
    for(unsigned queue=0;queue<2;++queue) {
        init(f,queue,sizeof(f->bytes));
        CHECK(kosdec_adx_loader_get_input_joint(&f->loader)==&f->joint);
        CHECK(!info(f,2,44100).sector_policy);
        CHECK(kosdec_adx_loader_set_reload_default(&f->loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(info(f,2,44100).reload_bytes==54*2048 && info(f,2,44100).sector_policy);
        const unsigned rates[]={8000,11025,16000,22050,24000,32000,44100,48000};
        const unsigned stereo_sectors[]={4,6,8,12,13,17,24,26};
        for(unsigned r=0;r<8;++r) {
            CHECK(kosdec_adx_loader_set_reload_time(&f->loader,1.0f,2,rates[r])==KOSDEC_ADX_LOADER_OK);
            CHECK(info(f,2,rates[r]).reload_bytes==stereo_sectors[r]*2048);
        }
        for(unsigned ch=1;ch<=2;++ch) {
            float edge=ch==1?114.0f:57.0f;
            CHECK(kosdec_adx_loader_set_reload_time(&f->loader,nextafterf(edge,0),ch,32)==KOSDEC_ADX_LOADER_OK);
            CHECK(info(f,ch,32).reload_bytes==0);
            CHECK(kosdec_adx_loader_set_reload_time(&f->loader,edge,ch,32)==KOSDEC_ADX_LOADER_OK);
            CHECK(info(f,ch,32).reload_bytes==2048);
            CHECK(kosdec_adx_loader_set_reload_time(&f->loader,FLT_MAX,ch,48000)==KOSDEC_ADX_LOADER_OK);
            CHECK(info(f,ch,48000).reload_bytes==64*2048);
        }
        for(unsigned n=0;n<=64;++n) {
            CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,n)==KOSDEC_ADX_LOADER_OK);
            CHECK(info(f,1,8000).reload_bytes==n*2048);
        }
        struct kosdec_adx_loader saved=f->loader;
        const float invalid[]={-1.0f,INFINITY,-INFINITY,NAN};
        for(unsigned i=0;i<4;++i) CHECK(kosdec_adx_loader_set_reload_time(&f->loader,invalid[i],2,44100)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_time(&f->loader,1,0,44100)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_time(&f->loader,1,3,44100)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_time(&f->loader,1,2,0)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_time(&f->loader,1,2,48001)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,65)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,UINT32_MAX)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(!memcmp(&saved,&f->loader,sizeof(saved)));
        struct kosdec_adx_loader_buffer_info before=info(f,2,44100), out=before;
        CHECK(!kosdec_adx_loader_get_buffer_info(&f->loader,0,44100,&out));
        CHECK(!memcmp(&out,&before,sizeof(out)));
        CHECK(kosdec_adx_loader_set_flow(&f->loader,0)==KOSDEC_ADX_LOADER_OK);
        CHECK(!info(f,1,1).sector_policy);
        close_fixture(f);
        init(f,queue,2047);saved=f->loader;
        CHECK(kosdec_adx_loader_set_reload_default(&f->loader)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,0)==KOSDEC_ADX_LOADER_BAD_ARGUMENT);
        CHECK(!memcmp(&saved,&f->loader,sizeof(saved)));close_fixture(f);
    }
    free(f);
}
static void counts(void) {
    struct fixture *f=malloc(sizeof(*f));CHECK(f);
    for(unsigned queue=0;queue<2;++queue) for(unsigned bytes=0;bytes<=4097;++bytes) {
        init(f,queue,8192);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,1)==KOSDEC_ADX_LOADER_OK);
        if(bytes) publish(f,bytes);
        for(unsigned ch=1;ch<=2;++ch) {
            /* At sample rate32, every complete block/channel group is 1 second. */
            struct kosdec_adx_loader_buffer_info i=info(f,ch,32);
            CHECK(i.queued_bytes==bytes && !i.held_bytes && i.sectors==bytes/2048);
            CHECK(i.seconds==(float)(bytes/(18*ch)) && i.safe==(bytes>=2048));
        }
        struct kosdec_adx_chunk held;
        CHECK(kosdec_adx_joint_get(&f->joint,KOSDEC_ADX_JOINT_DATA,bytes,&held)==KOSDEC_ADX_JOINT_OK);
        struct kosdec_adx_loader_buffer_info i=info(f,2,32);
        CHECK(!i.queued_bytes && i.held_bytes==bytes && !i.sectors && !i.seconds && !i.safe);
        CHECK(kosdec_adx_joint_put(&f->joint,&held)==KOSDEC_ADX_JOINT_OK);
        close_fixture(f);
    }
    free(f);
}
static struct kosdec_adx_input_result read_at(void *ctx,uint32_t off,uint8_t *dst,size_t bytes) {
    (void)ctx;CHECK(off<8192 && bytes && bytes<=512);++calls;
    memset(dst,0,bytes);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,bytes};
}
static void admission(void) {
    struct fixture *f=malloc(sizeof(*f));CHECK(f);
    for(unsigned queue=0;queue<2;++queue) {
        init(f,queue,8192);calls=0;uint64_t id;
        CHECK(kosdec_adx_loader_enqueue(&f->loader,"adx",read_at,NULL,8192,0,4,&id)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_start(&f->loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,0)==KOSDEC_ADX_LOADER_OK);
        CHECK(info(f,2,44100).safe);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && !calls);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,1)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_OK && calls==1);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,0)==KOSDEC_ADX_LOADER_OK);
        for(unsigned step=0;step<8 && f->loader.active;++step)
            CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(calls==4 && info(f,2,44100).queued_bytes==2048);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && calls==4);
        CHECK(kosdec_adx_loader_set_reload_sectors(&f->loader,1)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && calls==4);
        struct kosdec_adx_chunk held;
        CHECK(kosdec_adx_joint_get(&f->joint,KOSDEC_ADX_JOINT_DATA,2048,&held)==KOSDEC_ADX_JOINT_OK);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_OK && calls==5);
        for(unsigned step=0;step<8 && f->loader.active;++step)
            CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_set_flow(&f->loader,2048)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_NEED_OUTPUT && calls==8);
        CHECK(kosdec_adx_loader_set_flow(&f->loader,0)==KOSDEC_ADX_LOADER_OK);
        CHECK(kosdec_adx_loader_step(&f->loader)==KOSDEC_ADX_LOADER_OK && calls==9);
        CHECK(kosdec_adx_joint_put(&f->joint,&held)==KOSDEC_ADX_JOINT_OK);
        close_fixture(f);
    }
    free(f);
}
int main(void) {
    controls();counts();admission();
    CHECK(!kosdec_adx_loader_get_input_joint(NULL));
    printf("ADX_INPUT_POLICY checks=%u failures=0\n",checks);return 0;
}
