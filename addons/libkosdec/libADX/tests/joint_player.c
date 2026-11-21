#include <kosdec/adx_player.h>
#include <kosdec/adx_source.h>
#include "player_mock.h"
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
static unsigned checks;
#define CHECK(x) do { __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED); if(!(x)) { \
    fprintf(stderr,"joint_player:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
struct fixture {
    struct kosdec_adx_player player;
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_player_config config;
    uint8_t *data,ring[192]; size_t bytes,position;
    unsigned calls,waits_at_end;
    bool wait,fail,worker_only;
    pthread_t controller;
    alignas(32) int16_t pcm[1024],scratch[257];
    alignas(32) uint8_t stack[16384];
};
/* Assertions called by a worker use an atomic counter too. */
static void worker_check(int value,int line) {
    __atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);
    if(!value) {fprintf(stderr,"joint_player feed:%d\n",line);exit(1);}
}
#define WC(x) worker_check(!!(x),__LINE__)
static enum kosdec_adx_joint_feed_status feed(void *ctx,struct kosdec_adx_joint *j) {
    struct fixture *f=ctx;
    WC(j==&f->joint);
    WC(!f->worker_only || !pthread_equal(pthread_self(),f->controller));
    __atomic_add_fetch(&f->calls,1,__ATOMIC_RELAXED);
    if(__atomic_load_n(&f->fail,__ATOMIC_ACQUIRE)) return KOSDEC_ADX_JOINT_FEED_ERROR;
    if(__atomic_load_n(&f->wait,__ATOMIC_ACQUIRE)) return KOSDEC_ADX_JOINT_FEED_WAIT;
    if(f->position==f->bytes) {
        if(f->waits_at_end++<3) return KOSDEC_ADX_JOINT_FEED_WAIT;
        return KOSDEC_ADX_JOINT_FEED_END;
    }
    struct kosdec_adx_chunk c;
    size_t n=f->bytes-f->position; if(n>17)n=17;
    WC(kosdec_adx_joint_get(j,KOSDEC_ADX_JOINT_FREE,n,&c)==KOSDEC_ADX_JOINT_OK);
    if(c.bytes) memcpy(c.data,f->data+f->position,c.bytes);
    WC(kosdec_adx_joint_put(j,&c)==KOSDEC_ADX_JOINT_OK);
    f->position+=c.bytes;
    return KOSDEC_ADX_JOINT_FEED_MORE;
}
static unsigned calls(struct fixture *f) {return __atomic_load_n(&f->calls,__ATOMIC_RELAXED);}
static void source(struct fixture *f) {
    f->position=0;f->waits_at_end=0;
    OK(kosdec_adx_joint_ring_init(&f->joint,f->ring,128,64));
    OK(kosdec_adx_joint_reader_init(&f->reader,&f->joint));
    CHECK(kosdec_adx_joint_reader_set_feed(&f->reader,feed,f)==KOSDEC_ADX_DONE);
}
static void init(struct fixture *f,unsigned channels,bool worker) {
    memset(f,0,sizeof(*f));mock_reset();f->data=stream(50,18,channels,32,65,&f->bytes);
    f->controller=pthread_self();f->worker_only=worker;source(f);
    f->config=(struct kosdec_adx_player_config){.pcm=f->pcm,.scratch=f->scratch,
        .pcm_samples=1024,.capacity=512,.scratch_samples=257,.stack=f->stack,
        .stack_bytes=sizeof(f->stack),.sound_bytes=512,.transfer_timeout_ms=1000,.prefill_timeout_ms=1000};
    CHECK(!kosdec_adx_player_init_joint(&f->player,&f->reader,&f->config));
}
static struct kosdec_adx_player_info info(struct fixture *f) {
    struct kosdec_adx_player_info i;CHECK(!kosdec_adx_player_get_info(&f->player,&i));return i;
}
static int finish(struct fixture *f) {
    for(unsigned i=0;i<2000;++i) {
        if(kosdec_adx_player_poll(&f->player)<0)return -1;
        if(info(f).state==KOSDEC_ADX_PLAYER_SOURCE_END)return 0;
        mock_pause();
    }
    CHECK(0);return -1;
}

static void close_source(struct fixture *f) {
    OK(kosdec_adx_joint_reader_close(&f->reader));OK(kosdec_adx_joint_close(&f->joint));
}

static void memory_loan_player(void) {
    struct fixture f;
    init(&f,2,false);
    close_source(&f);
    memset(f.data+20,0,24);
    struct kosdec_adx_source_handle source;
    struct kosdec_adx_source loan = {0};
    CHECK(kosdec_adx_source_init_memory(&source,f.data,(uint32_t)f.bytes)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_borrow(&source,&loan,0)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&f.joint)==KOSDEC_ADX_DONE);
    OK(kosdec_adx_joint_reader_init(&f.reader,&f.joint));
    CHECK(!kosdec_adx_player_init_joint(&f.player,&f.reader,&f.config));
    CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(!finish(&f));
    struct kosdec_adx_player_input input;
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && input.joint==&f.joint &&
          !input.queued_bytes && !input.seconds && mock_frames==65);
    CHECK(!kosdec_adx_player_release_source(&f.player));
    close_source(&f);
    CHECK(kosdec_adx_source_return(&source,&loan)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_DONE);

    enum { MEMBER_OFFSET=81, MEMBER_BODY=50+2*36, MEMBER_BYTES=MEMBER_BODY+18 };
    uint8_t archive[MEMBER_OFFSET+MEMBER_BYTES+7] = {0};
    put32(archive+4,2);
    put32(archive+8,32); put32(archive+12,8);
    put32(archive+16,MEMBER_OFFSET); put32(archive+20,MEMBER_BYTES);
    memcpy(archive+MEMBER_OFFSET,f.data,MEMBER_BODY);
    put32(archive+MEMBER_OFFSET+12,33);
    memcpy(archive+MEMBER_OFFSET+MEMBER_BODY,f.data+f.bytes-18,18);
    memset(archive+MEMBER_OFFSET+MEMBER_BYTES,0xa5,7);
    CHECK(kosdec_adx_source_init_memory(&source,archive,sizeof(archive))==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_index_begin(&source,KOSDEC_ADX_ACX,1)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_source_index_step(&source)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_source_index_step(&source)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_index_begin(&source,KOSDEC_ADX_ACX,2)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_source_index_step(&source)==KOSDEC_ADX_NOT_FOUND);
    CHECK(kosdec_adx_source_borrow(&source,&loan,0)==KOSDEC_ADX_DONE && loan.bytes==MEMBER_BYTES);
    CHECK(kosdec_adx_source_init_memory_joint(&source,&loan,&f.joint)==KOSDEC_ADX_DONE);
    OK(kosdec_adx_joint_reader_init(&f.reader,&f.joint));
    CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));
    CHECK(!finish(&f));
    CHECK(mock_frames==98 && mock_allocs==1 && mock_starts==1);
    for(unsigned i=0;i<98;++i) for(unsigned channel=0;channel<2;++channel)
        CHECK(mock_pcm[2*i+channel]==golden[channel][i%65]);
    CHECK(!kosdec_adx_player_stop(&f.player,1000) && mock_joins==2);
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && !input.joint);
    close_source(&f);
    CHECK(kosdec_adx_source_return(&source,&loan)==KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_source_close(&source)==KOSDEC_ADX_DONE);
    free(f.data);
}

struct input_gate {
    unsigned calls, entered, allowed;
};
static enum kosdec_adx_joint_feed_status gate_input(void *context, struct kosdec_adx_joint *joint) {
    (void)joint;
    struct input_gate *gate = context;
    unsigned step = ++gate->calls;
    if(step <= 2) {
        __atomic_store_n(&gate->entered,step,__ATOMIC_RELEASE);
        while(__atomic_load_n(&gate->allowed,__ATOMIC_ACQUIRE) < step) mock_pause();
    }
    return KOSDEC_ADX_JOINT_FEED_WAIT;
}
static void wait_input_gate(struct input_gate *gate, unsigned step) {
    for(unsigned i = 0; i < 2000 && __atomic_load_n(&gate->entered,__ATOMIC_ACQUIRE) < step; ++i)
        mock_pause();
    CHECK(__atomic_load_n(&gate->entered,__ATOMIC_ACQUIRE) == step);
}
static void input_snapshot(void) {
    struct fixture f;
    struct input_gate gate = {0};
    uint8_t ring[16384];
    struct kosdec_adx_player_input input;
    init(&f,2,false);
    close_source(&f);
    put32(f.data + 12,65536);
    memset(f.data + 20,0,24);
    OK(kosdec_adx_joint_ring_init(&f.joint,ring,sizeof(ring),0));
    struct kosdec_adx_chunk chunk;
    OK(kosdec_adx_joint_get(&f.joint,KOSDEC_ADX_JOINT_FREE,4096,&chunk));
    CHECK(chunk.bytes == 4096);
    memset(chunk.data,0,chunk.bytes);
    memcpy(chunk.data,f.data,50);
    OK(kosdec_adx_joint_put(&f.joint,&chunk));
    OK(kosdec_adx_joint_reader_init(&f.reader,&f.joint));
    CHECK(kosdec_adx_joint_reader_enable_linked(&f.reader) == KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_feed(&f.reader,gate_input,&gate) == KOSDEC_ADX_DONE);
    f.config.use_lsc_reload = true;
    CHECK(!kosdec_adx_player_init_joint(&f.player,&f.reader,&f.config));
    input.queued_bytes = 123;
    CHECK(kosdec_adx_player_get_input(NULL,&input) < 0 && errno == EINVAL && input.queued_bytes == 123);
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && !input.joint && !input.queued_bytes &&
          !input.sectors && !input.seconds && !input.safe);
    CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,2));
    CHECK(!kosdec_adx_player_start(&f.player));
    wait_input_gate(&gate,1);
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && input.joint == &f.joint &&
          input.queued_bytes == 4096 && input.sectors == 2 && !input.seconds &&
          input.safety_limit_bytes == 4096 && input.safe);
    __atomic_store_n(&gate.allowed,1,__ATOMIC_RELEASE);
    wait_input_gate(&gate,2);
    CHECK(info(&f).state == KOSDEC_ADX_PLAYER_PREFILL);
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && input.joint == &f.joint &&
          input.queued_bytes == 3584 && input.sectors == 1 && input.seconds == 3168.0f / 44100.0f &&
          input.safety_limit_bytes == 4096 && !input.safe);
    CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,1));
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && input.queued_bytes == 3584 &&
          input.safety_limit_bytes == 2048 && input.safe);
    CHECK(!kosdec_adx_player_set_linked_reload_time(&f.player,0.2f,2,44100));
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && input.queued_bytes == 3584 &&
          input.safety_limit_bytes == 8192 && !input.safe);
    __atomic_store_n(&gate.allowed,2,__ATOMIC_RELEASE);
    CHECK(!kosdec_adx_player_cancel_source(&f.player));
    CHECK(!kosdec_adx_player_get_input(&f.player,&input) && !input.joint && !input.queued_bytes &&
          !input.sectors && !input.seconds && !input.safe);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    close_source(&f);
    free(f.data);
}

struct reload_input {
    struct kosdec_adx_loader *loader;
    const uint8_t *data;
    size_t bytes, expected_limit;
    unsigned entered;
    bool allow;
};
static struct kosdec_adx_input_result reload_read(void *context, uint32_t offset,
    uint8_t *dst, size_t bytes) {
    struct reload_input *source = context;
    struct kosdec_adx_loader_info loader;
    struct kosdec_adx_loader_buffer_info buffer;
    WC(kosdec_adx_loader_get_info(source->loader,&loader) && loader.flow_limit == source->expected_limit);
    WC(kosdec_adx_loader_get_buffer_info(source->loader,2,44100,&buffer) &&
       buffer.reload_bytes == source->expected_limit);
    __atomic_store_n(&source->entered,1,__ATOMIC_RELEASE);
    if(!__atomic_load_n(&source->allow,__ATOMIC_ACQUIRE))
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    WC(bytes <= 512 && offset < source->bytes && bytes <= source->bytes - offset);
    memcpy(dst,source->data+offset,bytes);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,bytes};
}
static void prepare_reload(struct fixture *f, struct kosdec_adx_loader *loader,
                           uint8_t *ring, size_t capacity) {
    OK(kosdec_adx_joint_ring_init(&f->joint,ring,capacity,0));
    CHECK(kosdec_adx_loader_init(loader,&f->joint) == KOSDEC_ADX_LOADER_OK);
    OK(kosdec_adx_joint_reader_init(&f->reader,&f->joint));
    CHECK(kosdec_adx_joint_reader_enable_linked(&f->reader) == KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_loader(&f->reader,loader,true) == KOSDEC_ADX_DONE);
}
static void linked_reload(void) {
    struct fixture f;
    struct kosdec_adx_loader loader;
    uint8_t ring[16384];
    init(&f,2,false);
    CHECK(kosdec_adx_player_set_linked_reload_sectors(&f.player,1) < 0 && errno == ENOTSUP);
    close_source(&f);
    /* The shared fixture defaults to an embedded loop. Linked records are linear. */
    memset(f.data + 20,0,24);
    f.config.use_lsc_reload = true;
    prepare_reload(&f,&loader,ring,sizeof(ring));
    CHECK(!kosdec_adx_player_init_joint(&f.player,&f.reader,&f.config));
    struct kosdec_adx_loader_info status;
    CHECK(kosdec_adx_loader_start(&loader) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_player_start(&f.player) < 0 && errno == EBUSY);
    CHECK(kosdec_adx_loader_get_info(&loader,&status) &&
          status.state == KOSDEC_ADX_LOADER_WAIT && status.flow_limit == 13107);
    CHECK(kosdec_adx_loader_stop(&loader) == KOSDEC_ADX_LOADER_OK);
    struct reload_input first = {.loader=&loader,.data=f.data,.bytes=f.bytes,.expected_limit=4096};
    struct reload_input second = first;
    second.allow = true;
    uint64_t id;
    CHECK(kosdec_adx_loader_enqueue(&loader,"first",reload_read,&first,(uint32_t)f.bytes,0,1,&id) == KOSDEC_ADX_LOADER_OK);
    CHECK(kosdec_adx_loader_enqueue(&loader,"second",reload_read,&second,(uint32_t)f.bytes,0,1,&id) == KOSDEC_ADX_LOADER_OK);
    CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,9));
    CHECK(kosdec_adx_player_start(&f.player) < 0 && errno == EINVAL);
    CHECK(info(&f).state == KOSDEC_ADX_PLAYER_READY && mock_allocs == 0);
    CHECK(kosdec_adx_loader_get_info(&loader,&status) &&
          status.state == KOSDEC_ADX_LOADER_STOPPED && status.pending == 2 && status.flow_limit == 13107);
    CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,2));
    struct kosdec_adx_chunk old_data;
    OK(kosdec_adx_joint_get(&f.joint,KOSDEC_ADX_JOINT_FREE,1,&old_data));
    CHECK(old_data.bytes == 1);
    OK(kosdec_adx_joint_put(&f.joint,&old_data));
    CHECK(kosdec_adx_player_start(&f.player) < 0 && errno == EINVAL);
    OK(kosdec_adx_joint_get(&f.joint,KOSDEC_ADX_JOINT_DATA,1,&old_data));
    CHECK(old_data.bytes == 1);
    OK(kosdec_adx_joint_put(&f.joint,&old_data));
    CHECK(!kosdec_adx_player_start(&f.player));
    for(unsigned i = 0; i < 2000 && !__atomic_load_n(&first.entered,__ATOMIC_ACQUIRE); ++i)
        mock_pause();
    CHECK(__atomic_load_n(&first.entered,__ATOMIC_ACQUIRE));
    CHECK(!kosdec_adx_player_set_linked_reload_sectors(&f.player,1));
    CHECK(!kosdec_adx_player_set_linked_reload_time(&f.player,0.2f,2,44100));
    CHECK(kosdec_adx_player_set_linked_reload_time(&f.player,-1.0f,2,44100) < 0 && errno == EINVAL);
    __atomic_store_n(&first.allow,true,__ATOMIC_RELEASE);
    CHECK(!finish(&f));
    CHECK(!kosdec_adx_player_release_source(&f.player));
    CHECK(second.entered && mock_frames == 130 && f.reader.linked.completed_records == 2);
    CHECK(kosdec_adx_loader_get_info(&loader,&status) && status.flow_limit == 4096 && !status.pending);
    CHECK(kosdec_adx_loader_close(&loader) == KOSDEC_ADX_LOADER_OK);
    close_source(&f);

    prepare_reload(&f,&loader,ring,6144);
    struct reload_input restart = {.loader=&loader,.data=f.data,.bytes=f.bytes,
                                   .expected_limit=6144,.allow=true};
    CHECK(kosdec_adx_loader_enqueue(&loader,"restart",reload_read,&restart,(uint32_t)f.bytes,0,1,&id) == KOSDEC_ADX_LOADER_OK);
    CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));
    CHECK(!finish(&f));
    CHECK(!kosdec_adx_player_stop(&f.player,1000));
    CHECK(restart.entered && mock_frames == 195 && mock_allocs == 1 && mock_starts == 1 && mock_joins == 2);
    CHECK(kosdec_adx_loader_close(&loader) == KOSDEC_ADX_LOADER_OK);
    close_source(&f);
    free(f.data);
}
static void playback(void) {
    for(unsigned ch=1;ch<=2;++ch) {
        struct fixture f;init(&f,ch,true);
        CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
        CHECK(info(&f).decoder==KOSDEC_ADX_DONE && mock_frames==65);
        CHECK(mock_allocs==1 && mock_starts==1);
        CHECK(!kosdec_adx_player_release_source(&f.player));
        unsigned before=calls(&f);close_source(&f);source(&f);
        CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));CHECK(!finish(&f));
        CHECK(calls(&f)>before && mock_frames==130 && mock_allocs==1 && mock_starts==1);
        for(size_t i=0;i<130;++i)for(unsigned c=0;c<ch;++c)
            CHECK(mock_pcm[i*ch+c]==golden[c][i%65]);
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        CHECK(mock_joins==2 && info(&f).retained_sound==7);
        before=calls(&f);for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before);
        close_source(&f);free(f.data);
    }
}
static void job_playback(void) {
    struct fixture f;
    int16_t work[256];
    init(&f, 2, true);
    CHECK(kosdec_adx_joint_reader_enable_jobs(&f.reader, work, 256, 128, 31, false) ==
        KOSDEC_ADX_DONE);
    CHECK(!kosdec_adx_player_start(&f.player));
    CHECK(!finish(&f));
    CHECK(info(&f).decoder == KOSDEC_ADX_DONE && mock_frames == 65);
    for(size_t i = 0; i < 65; ++i)
        for(unsigned ch = 0; ch < 2; ++ch)
            CHECK(mock_pcm[i * 2 + ch] == golden[ch][i]);
    CHECK(!kosdec_adx_player_stop(&f.player, 1000));
    CHECK(mock_allocs == 1 && mock_starts == 1 && mock_joins == 1);
    close_source(&f);
    free(f.data);
}
static void failures(void) {
    for(unsigned fail=0;fail<2;++fail) {
        struct fixture f;init(&f,2,true);f.wait=!fail;f.fail=fail;
        CHECK(!kosdec_adx_player_start(&f.player));
        if(fail) {CHECK(finish(&f)<0);CHECK(info(&f).first_error==EIO);}
        else {
            for(unsigned i=0;i<2000 && !calls(&f);++i)mock_pause();
            CHECK(calls(&f)>0);CHECK(!kosdec_adx_player_cancel_source(&f.player));
        }
        CHECK(!kosdec_adx_player_stop(&f.player,1000));
        unsigned before=calls(&f);for(unsigned i=0;i<4;++i)mock_pause();CHECK(calls(&f)==before);
        CHECK(!mock_allocs && mock_joins==1);
        close_source(&f);free(f.data);
    }
    struct fixture f;init(&f,1,true);
    CHECK(!kosdec_adx_player_start(&f.player));CHECK(!finish(&f));
    CHECK(!kosdec_adx_player_release_source(&f.player));close_source(&f);free(f.data);
    f.data=stream(50,18,2,32,65,&f.bytes);source(&f);
    CHECK(!kosdec_adx_player_bind_joint(&f.player,&f.reader));
    CHECK(finish(&f)<0 && info(&f).first_error==EPROTO && mock_frames==65);
    CHECK(!kosdec_adx_player_stop(&f.player,1000));close_source(&f);free(f.data);
}
static struct kosdec_adx_input_result unused_input(void *c,uint32_t o,uint8_t *d,size_t n) {
    (void)c;(void)o;(void)d;(void)n;CHECK(0);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
}
static void service(void) {
    struct fixture f;init(&f,2,false);struct kosdec_adx_service s;
    CHECK(kosdec_adx_service_init_joint(&s,&f.reader,f.pcm,1024,32)==KOSDEC_ADX_MORE);
    for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(!calls(&f));
    int16_t pcm[64];
    CHECK(kosdec_adx_joint_reader_read(&f.reader,pcm,0).status==KOSDEC_ADX_NEED_OUTPUT);
    CHECK(!calls(&f));
    struct kosdec_adx_command cmd={.kind=KOSDEC_ADX_PRODUCTION_PAUSE};
    enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_MORE && !calls(&f));
    for(unsigned i=0;i<100 && s.pipe.write_cursor!=32;++i) {
        unsigned before=calls(&f);CHECK(kosdec_adx_service_step(&s).status<KOSDEC_ADX_DONE);
        CHECK(calls(&f)-before<=1);
    }
    CHECK(s.pipe.write_cursor==32);unsigned before=calls(&f);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_NEED_OUTPUT && calls(&f)==before);
    CHECK(kosdec_adx_pipe_read(&s.pipe,pcm,32).frames==32 && calls(&f)==before);
    for(unsigned i=0;i<32;++i)for(unsigned c=0;c<2;++c) CHECK(pcm[2*i+c]==golden[c][i]);
    cmd.enabled=true;CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE && calls(&f)==before);
    for(unsigned kind=KOSDEC_ADX_SEQUENCE_REPEAT;kind<=KOSDEC_ADX_ENQUEUE;++kind) {
        cmd.kind=(enum kosdec_adx_command_kind)kind;cmd.source.read_at=unused_input;cmd.source.bytes=1;
        CHECK(kosdec_adx_service_post(&s,&cmd)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_take(&s,&receipt) && receipt==KOSDEC_ADX_UNSUPPORTED);
    }
    kosdec_adx_service_request_cancel(&s);
    CHECK(kosdec_adx_service_step(&s).status==KOSDEC_ADX_CANCELLED && calls(&f)==before);
    close_source(&f);free(f.data);
}
int main(void) {
    service();playback();job_playback();linked_reload();input_snapshot();memory_loan_player();failures();
    printf("joint_player: %u checks passed, PCM/controls/feed ownership/rebind/errors\n",checks);
    return 0;
}
