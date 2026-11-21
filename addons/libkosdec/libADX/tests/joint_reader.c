#include <kosdec/adx_joint_reader.h>
#include <kosdec/adx_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
static unsigned checks, cases, filtered;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"joint_reader:%d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==KOSDEC_ADX_JOINT_OK)
#define DATA KOSDEC_ADX_JOINT_DATA
#define FREE KOSDEC_ADX_JOINT_FREE
static void filter(void *context,unsigned channel,int16_t *pcm,size_t frames,unsigned stride) {
    CHECK(context==&filtered && channel<stride);
    for(size_t i=0;i<frames;++i) pcm[i*stride]=(int16_t)-pcm[i*stride];
    filtered+=(unsigned)frames;
}
static void playback(unsigned kind,unsigned ch,unsigned header,unsigned tail,
                     unsigned fragment,int effect) {
    size_t size;
    uint8_t *data=stream(header,tail,ch,32,65,&size),ring[100];
    struct kosdec_adx_joint j;
    struct kosdec_adx_joint_node nodes[8];
    if(kind==0) OK(kosdec_adx_joint_memory_init(&j,data,size));
    else if(kind==1) OK(kosdec_adx_joint_ring_init(&j,ring,67,33));
    else OK(kosdec_adx_joint_queue_init(&j,nodes,8,
        kind==2 ? KOSDEC_ADX_JOINT_JOIN : KOSDEC_ADX_JOINT_SEPARATE));
    struct kosdec_adx_joint_reader reader;
    OK(kosdec_adx_joint_reader_init(&reader,&j));
    filtered=0;
    if(effect) CHECK(kosdec_adx_joint_reader_set_filter(&reader,filter,&filtered)==KOSDEC_ADX_DONE);
    size_t supplied=kind==0 ? size : 0, frames=0;
    int end_wait=0,done=0;
    for(unsigned step=0;step<20000;++step) {
        struct kosdec_adx_joint_info info;
        OK(kosdec_adx_joint_query(&j,&info));
        if(kind>=2 && !info.sealed) {
            struct kosdec_adx_chunk free;
            OK(kosdec_adx_joint_get(&j,FREE,SIZE_MAX,&free));
            OK(kosdec_adx_joint_detach(&j,&free));
        }
        if(supplied<size && step%5) {
            size_t n=size-supplied; if(n>fragment)n=fragment;
            if(kind==1) {
                struct kosdec_adx_chunk c;
                OK(kosdec_adx_joint_get(&j,FREE,n,&c));
                if(c.bytes) memcpy(c.data,data+supplied,c.bytes);
                OK(kosdec_adx_joint_put(&j,&c)); supplied+=c.bytes;
            } else {
                enum kosdec_adx_joint_status status=kosdec_adx_joint_seed(&j,DATA,data+supplied,n);
                CHECK(status==KOSDEC_ADX_JOINT_OK || status==KOSDEC_ADX_JOINT_LIMIT);
                if(status==KOSDEC_ADX_JOINT_OK) supplied+=n;
            }
        }
        int16_t pcm[64];
        size_t capacity=step%9==0 ? 0 : step%7==0 ? 1 : 32;
        uint64_t before=reader.consumed_bytes;
        struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,capacity);
        CHECK(r.joint_status==KOSDEC_ADX_JOINT_OK);
        CHECK(reader.consumed_bytes-before<=512 && r.frames<=32);
        if(!capacity) CHECK(reader.consumed_bytes==before);
        for(size_t f=0;f<r.frames;++f)for(unsigned c=0;c<ch;++c) {
            CHECK(frames+f<65);
            int expected=golden[c][frames+f]; if(effect)expected=-expected;
            CHECK(pcm[f*ch+c]==expected);
        }
        frames+=r.frames;
        if(r.status==KOSDEC_ADX_NEED_END) {
            CHECK(supplied==size && frames==65); ++end_wait;
            /* Repeat starvation once to show empty is not an automatic EOF. */
            if(end_wait==2) OK(kosdec_adx_joint_finish(&j));
        }
        if(r.status==KOSDEC_ADX_DONE) {done=1;break;}
        CHECK(r.status<KOSDEC_ADX_DONE);
    }
    CHECK(done && end_wait>=2 && frames==65 && reader.consumed_bytes==size);
    CHECK(filtered==(effect ? 65*ch : 0));
    CHECK(kosdec_adx_joint_reader_set_filter(&reader,NULL,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    OK(kosdec_adx_joint_reader_close(&reader)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void endings(unsigned kind,unsigned fault) {
    size_t size; uint8_t *data=stream(fault ? 50 : 1042,fault ? 18 : 1042,2,32,65,&size);
    enum kosdec_adx_status expected=KOSDEC_ADX_DONE;
    if(fault==1) {--size;expected=KOSDEC_ADX_TRUNCATED;}
    if(fault==2) {size=19;expected=KOSDEC_ADX_TRUNCATED;}
    if(fault==3) {++size;expected=KOSDEC_ADX_INVALID;}
    if(fault==4) {data[50]|=0x80;expected=KOSDEC_ADX_INVALID;}
    struct kosdec_adx_joint j; struct kosdec_adx_joint_node node;
    if(kind==0) OK(kosdec_adx_joint_memory_init(&j,data,size));
    else {
        /* A sealed full pool cannot depend on a spare node to decode. */
        OK(kosdec_adx_joint_queue_init(&j,&node,1,
            kind==1 ? KOSDEC_ADX_JOINT_JOIN : KOSDEC_ADX_JOINT_SEPARATE));
        OK(kosdec_adx_joint_seed(&j,DATA,data,size));
    }
    OK(kosdec_adx_joint_finish(&j)); OK(kosdec_adx_joint_finish(&j));
    struct kosdec_adx_joint_reader r; OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64]; enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<20 && status<KOSDEC_ADX_DONE;++i)
        status=kosdec_adx_joint_reader_read(&r,pcm,32).status;
    CHECK(status==expected);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==expected);
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void controls(void) {
    size_t size; uint8_t *data=stream(50,18,1,32,65,&size);
    struct kosdec_adx_joint j,before;
    struct kosdec_adx_joint_reader r,save;
    struct kosdec_adx_joint_node nodes[3];
    struct kosdec_adx_chunk c,a,b;
    struct kosdec_adx_joint_info info;
    OK(kosdec_adx_joint_memory_init(&j,data,size));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64];
    save=r;
    CHECK(kosdec_adx_joint_reader_read(&r,NULL,1).status==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&save,&r,sizeof(r)));
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_MORE);
    CHECK(r.held.bytes && r.used==50);
    CHECK(kosdec_adx_joint_reader_init(&save,&j)==KOSDEC_ADX_JOINT_BUSY);
    /* Inject a stale descriptor to verify a failed release does not erase the
       real borrow. Direct field changes are not a supported caller operation. */
    uint64_t token=r.held.token; ++r.held.token; save=r;
    CHECK(kosdec_adx_joint_reader_close(&r)==KOSDEC_ADX_JOINT_STALE);
    CHECK(!memcmp(&save,&r,sizeof(r)));
    r.held.token=token;
    kosdec_adx_joint_reader_cancel(&r);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_CANCELLED);
    CHECK(kosdec_adx_joint_reset(&j)==KOSDEC_ADX_JOINT_BUSY);
    OK(kosdec_adx_joint_reader_close(&r));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(!info.available[DATA]);
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_BAD_ARGUMENT);
    OK(kosdec_adx_joint_reset(&j));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));

    uint8_t buffer[64];
    OK(kosdec_adx_joint_ring_init(&j,buffer,32,32));
    OK(kosdec_adx_joint_get(&j,FREE,1,&c)); before=j;
    CHECK(kosdec_adx_joint_finish(&j)==KOSDEC_ADX_JOINT_BUSY);
    CHECK(!memcmp(&before,&j,sizeof(j)));
    OK(kosdec_adx_joint_unget(&j,&c)); OK(kosdec_adx_joint_finish(&j));
    CHECK(kosdec_adx_joint_get(&j,FREE,1,&c)==KOSDEC_ADX_JOINT_SEALED);
    OK(kosdec_adx_joint_reset(&j)); OK(kosdec_adx_joint_close(&j));

    OK(kosdec_adx_joint_queue_init(&j,nodes,3,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,buffer,16));
    OK(kosdec_adx_joint_get(&j,DATA,16,&c));
    OK(kosdec_adx_chunk_split(&c,6,&a,&b));
    OK(kosdec_adx_joint_detach(&j,&a));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.capacity==10 && info.borrowed[DATA]==10);
    OK(kosdec_adx_joint_seed(&j,FREE,buffer,6)); /* Detached prefix is reusable. */
    OK(kosdec_adx_joint_finish(&j)); before=j;
    CHECK(kosdec_adx_joint_seed(&j,DATA,buffer+16,1)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(kosdec_adx_joint_move(&j,DATA,&b)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(kosdec_adx_joint_get(&j,KOSDEC_ADX_JOINT_AUX2,1,&a)==KOSDEC_ADX_JOINT_SEALED);
    CHECK(!memcmp(&before,&j,sizeof(j)));
    OK(kosdec_adx_joint_unget(&j,&b));
    OK(kosdec_adx_joint_get(&j,DATA,10,&c)); OK(kosdec_adx_joint_detach(&j,&c));
    OK(kosdec_adx_joint_query(&j,&info)); CHECK(info.capacity==6 && info.spare_nodes==2);
    OK(kosdec_adx_joint_reset(&j)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static void late_tail(void) {
    size_t size; uint8_t *data=stream(50,18,1,32,65,&size);
    struct kosdec_adx_joint j; struct kosdec_adx_joint_node nodes[2];
    struct kosdec_adx_joint_reader r;
    OK(kosdec_adx_joint_queue_init(&j,nodes,2,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_seed(&j,DATA,data,size));
    OK(kosdec_adx_joint_reader_init(&r,&j));
    int16_t pcm[64]; enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<20 && status!=KOSDEC_ADX_NEED_END;++i) {
        status=kosdec_adx_joint_reader_read(&r,pcm,32).status;
        CHECK(status<KOSDEC_ADX_DONE);
    }
    CHECK(status==KOSDEC_ADX_NEED_END);
    OK(kosdec_adx_joint_seed(&j,DATA,data+size,1));
    OK(kosdec_adx_joint_finish(&j));
    CHECK(kosdec_adx_joint_reader_read(&r,pcm,32).status==KOSDEC_ADX_INVALID);
    OK(kosdec_adx_joint_reader_close(&r)); OK(kosdec_adx_joint_close(&j));
    free(data); ++cases;
}
static uint8_t *job_source(unsigned start, unsigned end, unsigned offset,
                           unsigned footer, size_t *bytes) {
    uint8_t *data = stream(64, footer, 2, start, end, bytes);
    uint8_t *grown = realloc(data, *bytes + 36);
    if(!grown) exit(2);
    data = grown;
    memmove(data + 64 + 144, data + 64 + 108, footer);
    *bytes += 36;
    put32(data + 12, 127);
    put32(data + 32, 64 + offset);
    for(unsigned block = 0; block < 8; ++block) {
        uint8_t *b = data + 64 + 18 * block;
        b[0] = 0;
        b[1] = 3;
        for(unsigned i = 2; i < 18; ++i)
            b[i] = (uint8_t)(17 * i + 3 * block);
    }
    return data;
}
static uint32_t digest_pcm(const int16_t *pcm, size_t frames) {
    uint32_t digest = 0;
    for(unsigned ch = 0; ch < 2; ++ch)
        for(size_t i = 0; i < frames; ++i) {
            uint16_t word = (uint16_t)pcm[i * 2 + ch];
            digest = digest * 31 + (word & 255);
            digest = digest * 31 + (word >> 8);
        }
    return digest;
}

static void queue_jobs(void) {
    /* ADXSJD + SJUNI observations on 2.00J, two stereo groups at 44100 Hz. */
    static const struct {
        bool join;
        unsigned left, right, offset, maximum, steps;
        struct {
            unsigned room, frames, chunks, spares;
            enum kosdec_adx_status status;
            uint32_t checksum;
        } step[3];
    } schedules[] = {
        {true,17,55,17,64,3, {{31,0,1,31,KOSDEC_ADX_NEED_OUTPUT,0},
                             {64,64,0,31,KOSDEC_ADX_MORE,0x0270e257},
                             {64,0,0,31,KOSDEC_ADX_DONE,0}}},
        {false,17,55,17,64,2, {{31,0,2,30,KOSDEC_ADX_NEED_OUTPUT,0},
                              {64,0,2,30,KOSDEC_ADX_NEED_INPUT,0}}},
        {true,17,55,108,64,1, {{64,0,2,30,KOSDEC_ADX_NEED_INPUT,0}}},
        {false,36,36,36,64,3, {{64,32,1,30,KOSDEC_ADX_MORE,0x8bd502cd},
                              {64,32,0,30,KOSDEC_ADX_MORE,0xd488e39b},
                              {64,0,0,30,KOSDEC_ADX_DONE,0}}},
        {false,37,35,37,64,2, {{64,32,2,29,KOSDEC_ADX_MORE,0x8bd502cd},
                              {64,0,2,29,KOSDEC_ADX_NEED_INPUT,0}}},
        {true,18,54,18,63,3, {{64,32,1,30,KOSDEC_ADX_MORE,0x8bd502cd},
                             {64,32,0,31,KOSDEC_ADX_MORE,0xd488e39b},
                             {64,0,0,31,KOSDEC_ADX_DONE,0}}},
    };
    for(unsigned i=0;i<sizeof(schedules)/sizeof(schedules[0]);++i) {
        uint8_t header[KOSDEC_ADX_SFA_HEADER_BYTES],body[180]={0};
        CHECK(kosdec_adx_make_sfa_header(header,sizeof(header),2,44100,64)==KOSDEC_ADX_DONE);
        for(unsigned block=0;block<sizeof(body)/18;++block) {
            body[18*block+1]=3;
            for(unsigned b=2;b<18;++b) body[18*block+b]=(uint8_t)(17*b+3*block);
        }
        struct kosdec_adx_joint joint;
        struct kosdec_adx_joint_node nodes[32];
        struct kosdec_adx_joint_reader reader;
        struct kosdec_adx_chunk free;
        int16_t work[128],pcm[128];
        OK(kosdec_adx_joint_queue_init(&joint,nodes,32,
            schedules[i].join?KOSDEC_ADX_JOINT_JOIN:KOSDEC_ADX_JOINT_SEPARATE));
        OK(kosdec_adx_joint_reader_init(&reader,&joint));
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,work,128,
            schedules[i].maximum,31,true)==KOSDEC_ADX_UNSUPPORTED);
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,work,128,
            schedules[i].maximum,31,false)==KOSDEC_ADX_DONE);
        OK(kosdec_adx_joint_seed(&joint,DATA,header,17));
        OK(kosdec_adx_joint_seed(&joint,DATA,header+17,sizeof(header)-17));
        while(!reader.decoder.header_ready) {
            uint64_t before=reader.consumed_bytes;
            struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,64);
            CHECK(r.status==KOSDEC_ADX_MORE && !r.frames && reader.consumed_bytes>before);
            OK(kosdec_adx_joint_get(&joint,FREE,SIZE_MAX,&free));
            OK(kosdec_adx_joint_detach(&joint,&free));
        }
        OK(kosdec_adx_joint_seed(&joint,DATA,body,schedules[i].left));
        OK(kosdec_adx_joint_seed(&joint,DATA,body+schedules[i].offset,schedules[i].right));
        unsigned used=0;
        for(unsigned step=0;step<schedules[i].steps;++step) {
            unsigned room=schedules[i].step[step].room;
            for(unsigned j=0;j<128;++j) pcm[j]=-12;
            struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,room);
            CHECK(r.status==schedules[i].step[step].status && r.joint_status==KOSDEC_ADX_JOINT_OK);
            CHECK(r.frames==schedules[i].step[step].frames);
            CHECK(digest_pcm(pcm,r.frames)==schedules[i].step[step].checksum);
            for(unsigned j=2*r.frames;j<128;++j) CHECK(pcm[j]==-12);
            used+=36*(r.frames/32u);
            struct kosdec_adx_joint_info info;
            OK(kosdec_adx_joint_query(&joint,&info));
            CHECK(info.available[DATA]==72-used && info.available[FREE]==used);
            CHECK(info.chunks[DATA]==schedules[i].step[step].chunks);
            CHECK(info.spare_nodes==schedules[i].step[step].spares);
            CHECK(!info.borrowed[DATA] && reader.consumed_bytes==sizeof(header)+used);
        }
        if(used<72) {
            OK(kosdec_adx_joint_finish(&joint));
            CHECK(kosdec_adx_joint_reader_read(&reader,pcm,64).status==KOSDEC_ADX_TRUNCATED);
        }
        OK(kosdec_adx_joint_reader_close(&reader));
        OK(kosdec_adx_joint_close(&joint));
        ++cases;
    }
}

static void queue_node_exhaustion(void) {
    uint8_t header[KOSDEC_ADX_SFA_HEADER_BYTES], body[72]={0};
    CHECK(kosdec_adx_make_sfa_header(header,sizeof(header),2,44100,64)==KOSDEC_ADX_DONE);
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_node node;
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_joint_info info;
    struct kosdec_adx_chunk free;
    int16_t work[64], pcm[64];
    OK(kosdec_adx_joint_queue_init(&joint,&node,1,KOSDEC_ADX_JOINT_JOIN));
    OK(kosdec_adx_joint_reader_init(&reader,&joint));
    CHECK(kosdec_adx_joint_reader_enable_jobs(&reader,work,64,32,31,false)==KOSDEC_ADX_DONE);
    OK(kosdec_adx_joint_seed(&joint,DATA,header,sizeof(header)));
    CHECK(kosdec_adx_joint_reader_read(&reader,pcm,32).status==KOSDEC_ADX_MORE);
    OK(kosdec_adx_joint_get(&joint,FREE,sizeof(header),&free));
    OK(kosdec_adx_joint_detach(&joint,&free));
    OK(kosdec_adx_joint_seed(&joint,DATA,body,sizeof(body)));
    for(unsigned i=0;i<64;++i) pcm[i]=17;
    struct kosdec_adx_joint_read_result r=kosdec_adx_joint_reader_read(&reader,pcm,32);
    CHECK(r.status==KOSDEC_ADX_IO_ERROR && r.joint_status==KOSDEC_ADX_JOINT_LIMIT && !r.frames);
    CHECK(reader.decoder.frames_done==0 && reader.consumed_bytes==sizeof(header));
    for(unsigned i=0;i<64;++i) CHECK(pcm[i]==17);
    OK(kosdec_adx_joint_query(&joint,&info));
    CHECK(info.borrowed[DATA]==sizeof(body) && !info.available[FREE]);
    OK(kosdec_adx_joint_reader_close(&reader));
    OK(kosdec_adx_joint_query(&joint,&info));
    CHECK(!info.borrowed[DATA] && info.available[FREE]==sizeof(body));
    OK(kosdec_adx_joint_close(&joint));
    ++cases;
}

static void jobs(void) {
    /* Observations from the 2.00J memory-source scheduler on synthetic data,
       using the documented v3 profile at 44100 Hz / 500 Hz. */
    const struct {
        unsigned budget, end, offset, release, footer;
        struct {
            unsigned frames, bytes, position, source_byte, wraps;
            uint32_t digest;
        } steps[3];
    } vectors[] = {
        {63,63,36,3,18, {{31,36,31,100,0,0x7a41f7ae},
                         {32,36,33,100,1,0xd488e39b},
                         {30,36,33,100,2,0x57918304}}},
        {128,63,36,1,18, {{63,72,33,100,1,0x38494dfd},
                          {30,36,63,136,1,0x0cb123a3},
                          {64,72,127,208,1,0xc8f0f254}}},
        {128,62,36,0,72, {{62,72,62,136,0,0x6b0a3e2d},
                          {64,72,126,208,0,0x7d0c583c},
                          {0,0,126,208,0,0}}},
        {128,63,72,3,18, {{63,72,33,136,1,0x38494dfd},
                          {30,36,33,136,2,0x82fb9a26},
                          {30,36,33,136,3,0xcfc55e7a}}},
    };
    for(unsigned i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
        size_t bytes;
        uint8_t *data = job_source(33, vectors[i].end, vectors[i].offset,
                                   vectors[i].footer, &bytes);
        struct kosdec_adx_joint joint;
        struct kosdec_adx_joint_reader reader;
        struct kosdec_adx_info meta;
        int16_t work[256], pcm[256];
        CHECK(kosdec_adx_probe_header(data, bytes, &meta) == KOSDEC_ADX_UNSUPPORTED);
        OK(kosdec_adx_joint_memory_init(&joint, data, bytes));
        OK(kosdec_adx_joint_reader_init(&reader, &joint));
        CHECK(kosdec_adx_joint_reader_enable_jobs(&reader, work, 256,
            vectors[i].budget, 31, true) == KOSDEC_ADX_DONE);
        CHECK(kosdec_adx_joint_reader_read(&reader, pcm, 31).status == KOSDEC_ADX_NEED_OUTPUT);
        CHECK(kosdec_adx_joint_reader_read(&reader, pcm, 128).status == KOSDEC_ADX_MORE);
        CHECK(reader.consumed_bytes == 64 && !reader.held.bytes);
        CHECK(kosdec_adx_get_info(&reader.decoder, &meta) == KOSDEC_ADX_DONE);
        CHECK(meta.loop.start_frame == 33 && meta.loop.start_byte == 64 + vectors[i].offset);
        for(unsigned step = 0; step < 3; ++step) {
            if(step == vectors[i].release)
                CHECK(kosdec_adx_joint_reader_release_loop(&reader) == KOSDEC_ADX_MORE);
            uint64_t before = reader.consumed_bytes;
            for(unsigned k = 0; k < 256; ++k) pcm[k] = 123;
            struct kosdec_adx_joint_read_result result =
                kosdec_adx_joint_reader_read(&reader, pcm, 128);
            CHECK(result.status == (vectors[i].steps[step].frames ?
                KOSDEC_ADX_MORE : KOSDEC_ADX_DONE));
            CHECK(result.joint_status == KOSDEC_ADX_JOINT_OK);
            CHECK(result.frames == vectors[i].steps[step].frames);
            CHECK(reader.consumed_bytes - before == vectors[i].steps[step].bytes);
            CHECK(reader.decoder.frames_done == vectors[i].steps[step].position);
            CHECK(joint.position[DATA] == vectors[i].steps[step].source_byte);
            CHECK(reader.job.wraps == vectors[i].steps[step].wraps && !reader.held.bytes);
            CHECK(digest_pcm(pcm, result.frames) == vectors[i].steps[step].digest);
            for(size_t k = result.frames * 2; k < 256; ++k) CHECK(pcm[k] == 123);
        }
        OK(kosdec_adx_joint_reader_close(&reader));
        OK(kosdec_adx_joint_close(&joint));
        free(data);
        ++cases;
    }
}
static void job_pipe(bool malformed) {
    size_t bytes;
    uint8_t *data = job_source(33, 63, 36, 18, &bytes);
    if(malformed) data[64 + 54] |= 0x80; /* Right channel of the second group. */
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    struct kosdec_adx_service service;
    int16_t work[256], pcm[128], output[128];
    uint32_t offsets[64], wraps;
    OK(kosdec_adx_joint_memory_init(&joint, data, bytes));
    OK(kosdec_adx_joint_reader_init(&reader, &joint));
    CHECK(kosdec_adx_joint_reader_enable_jobs(&reader, work, 256,
        malformed ? 128 : 63, 31, true) == KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_service_init_joint(&service, &reader, pcm, 128, 64) == KOSDEC_ADX_MORE);
    struct kosdec_adx_pipe *pipe = &service.pipe;
    pipe->source_offsets = offsets;
    struct kosdec_adx_command command = {.kind = KOSDEC_ADX_PRODUCTION_PAUSE};
    enum kosdec_adx_status receipt;
    CHECK(kosdec_adx_service_post(&service, &command) == KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&service).status == KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_take(&service, &receipt) && receipt == KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_service_step(&service).status == KOSDEC_ADX_MORE);
    if(malformed) {
        CHECK(kosdec_adx_service_step(&service).status == KOSDEC_ADX_INVALID);
        CHECK(joint.position[DATA] == 64 && reader.consumed_bytes == 64);
        CHECK(!reader.decoder.frames_done && !reader.decoder.clips && !reader.held.bytes);
        CHECK(kosdec_adx_pipe_read(pipe, output, 64).frames == 0);
    }
    else {
        CHECK(kosdec_adx_service_step(&service).frames == 31);
        CHECK(kosdec_adx_service_step(&service).frames == 32);
        CHECK(kosdec_adx_service_step(&service).status == KOSDEC_ADX_NEED_OUTPUT);
        CHECK(kosdec_adx_pipe_read(pipe, output, 64).frames == 63);
        for(unsigned i = 0; i < 63; ++i) CHECK(offsets[i] == i);
        CHECK(kosdec_adx_service_step(&service).frames == 30);
        CHECK(kosdec_adx_pipe_read(pipe, output, 64).frames == 30);
        CHECK(digest_pcm(output, 30) == 0x57918304);
        for(unsigned i = 0; i < 30; ++i) CHECK(offsets[(63 + i) % 64] == 33 + i);
        CHECK(kosdec_adx_pipe_get_loop_count(pipe, &wraps) && wraps == 2);
        command.kind = KOSDEC_ADX_RELEASE_LOOP;
        CHECK(kosdec_adx_service_post(&service, &command) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_step(&service).frames == 0);
        CHECK(kosdec_adx_service_take(&service, &receipt) && receipt == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_service_step(&service).frames == 30);
        CHECK(kosdec_adx_pipe_read(pipe, output, 64).frames == 30);
        CHECK(kosdec_adx_pipe_get_loop_count(pipe, &wraps) && wraps == 2);
        CHECK(reader.decoder.frames_done == 63 && joint.position[DATA] == 136);
    }
    OK(kosdec_adx_joint_reader_close(&reader));
    OK(kosdec_adx_joint_close(&joint));
    free(data);
    ++cases;
}
enum {
    TEST_SECTOR = KOSDEC_ADX_REQUEST_SECTOR_BYTES,
    FILE_HEADER = 2012,
    FILE_BODY = 144,
    FILE_BYTES = 3 * TEST_SECTOR
};
struct sector_fixture {
    uint8_t bytes[FILE_BYTES];
    unsigned calls;
    bool stalled, fail;
};
static struct kosdec_adx_input_result sector_input(void *arg, uint32_t at,
    uint8_t *output, size_t bytes) {
    struct sector_fixture *f = arg;
    CHECK(bytes <= 512 && at <= FILE_BYTES && bytes <= FILE_BYTES - at);
    ++f->calls;
    if(f->fail) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR, 0};
    if(f->stalled) return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT, 0};
    memcpy(output, f->bytes + at, bytes);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, bytes};
}
static size_t queued(struct kosdec_adx_joint *joint) {
    struct kosdec_adx_joint_info info;
    OK(kosdec_adx_joint_query(joint, &info));
    return info.available[DATA];
}
static void prefetch(struct sector_fixture *f, struct kosdec_adx_joint_reader *r,
                     size_t bytes) {
    for(unsigned step = 0; queued(r->joint) < bytes; ++step) {
        CHECK(step < 40);
        unsigned before = f->calls;
        struct kosdec_adx_joint_read_result result = kosdec_adx_joint_reader_read(r, NULL, 0);
        CHECK(result.status == KOSDEC_ADX_MORE && !result.frames);
        CHECK(f->calls - before <= 1);
    }
}
struct logical_loop {
    unsigned start, end;
    uint32_t first_digest, second_digest;
};

static void sector_jobs(unsigned scenario, uint32_t raw_start,
                        enum kosdec_adx_status header_status,
                        const struct logical_loop *loop) {
    struct sector_fixture file = {0};
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_reader reader;
    uint8_t ring[4 * TEST_SECTOR + 64];
    int16_t work[256], pcm[256];
    size_t bytes;
    unsigned start = loop ? loop->start : 32;
    unsigned end = loop ? loop->end : 63;
    unsigned end_bytes = 36 * ((end + 31) / 32);
    uint8_t *body = job_source(start,end,36,18,&bytes);
    memcpy(file.bytes, body, 44);
    memcpy(file.bytes + FILE_HEADER - 6, "(c)CRI", 6);
    memcpy(file.bytes + FILE_HEADER, body + 64, bytes - 64);
    free(body);
    put32(file.bytes + 32, raw_start);
    put32(file.bytes + 40, FILE_HEADER + end_bytes);
    file.bytes[2] = (FILE_HEADER - 4) >> 8;
    file.bytes[3] = (uint8_t)(FILE_HEADER - 4);
    if(scenario == 5) memset(file.bytes + 20, 0, 24);
    OK(kosdec_adx_joint_ring_init(&joint, ring, 4 * TEST_SECTOR, 64));
    OK(kosdec_adx_joint_reader_init(&reader, &joint));
    CHECK(kosdec_adx_joint_reader_enable_jobs(&reader, work, 256, 128, 31, false) ==
        KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_sector_source(&reader, sector_input, &file,
        FILE_BYTES, 2, true) == KOSDEC_ADX_DONE);
    CHECK(kosdec_adx_joint_reader_set_feed(&reader, NULL, NULL) == KOSDEC_ADX_BAD_ARGUMENT);
    struct kosdec_adx_info metadata;
    if(loop || raw_start != TEST_SECTOR)
        CHECK(kosdec_adx_probe_header(file.bytes, FILE_HEADER, &metadata) ==
              (loop ? KOSDEC_ADX_UNSUPPORTED : KOSDEC_ADX_INVALID));
    for(unsigned i = 0; !reader.decoder.header_ready; ++i) {
        CHECK(i < 20);
        struct kosdec_adx_joint_read_result result =
            kosdec_adx_joint_reader_read(&reader, pcm, 128);
        CHECK(!result.frames);
        if(result.status >= KOSDEC_ADX_DONE) {
            CHECK(result.status == header_status && !reader.job.wraps);
            CHECK(reader.sector.request.cursor == 1 && !reader.sector.configured);
            OK(kosdec_adx_joint_reader_close(&reader));
            OK(kosdec_adx_joint_close(&joint));
            ++cases;
            return;
        }
        CHECK(result.status == KOSDEC_ADX_MORE);
    }
    CHECK(header_status == KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_get_info(&reader.decoder, &metadata) == KOSDEC_ADX_DONE);
    if(scenario != 5)
        CHECK(metadata.loop.present && metadata.loop.start_byte == raw_start &&
              metadata.loop.start_frame == start && metadata.loop.end_frame == end);
    CHECK(reader.consumed_bytes == FILE_HEADER && queued(&joint) == 36 &&
          reader.sector.request.cursor == 1);
    if(scenario == 5) {
        struct kosdec_adx_pipe pipe;
        int16_t buffer[64];
        CHECK(kosdec_adx_pipe_init_joint(&pipe, &reader, buffer, 64, 32) == KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_pipe_pump(&pipe).frames == 32);
        for(unsigned i = 0; queued(&joint) < 2 * TEST_SECTOR; ++i) {
            CHECK(i < 20);
            struct kosdec_adx_stream_result result = kosdec_adx_pipe_pump(&pipe);
            CHECK(result.status == KOSDEC_ADX_MORE && !result.frames);
            CHECK(pipe.write_cursor == 32 && reader.decoder.frames_done == 32);
        }
        CHECK(kosdec_adx_pipe_pump(&pipe).status == KOSDEC_ADX_NEED_OUTPUT);
        CHECK(kosdec_adx_pipe_read(&pipe, pcm, 32).frames == 32);
        OK(kosdec_adx_joint_reader_close(&reader));
        OK(kosdec_adx_joint_close(&joint));
        ++cases;
        return;
    }
    if(scenario == 3 || scenario == 4) {
        CHECK(kosdec_adx_joint_reader_read(&reader, NULL, 0).status == KOSDEC_ADX_MORE);
        file.stalled = true;
        CHECK(kosdec_adx_joint_reader_read(&reader, NULL, 0).status == KOSDEC_ADX_NEED_INPUT);
        struct kosdec_adx_joint_info info;
        OK(kosdec_adx_joint_query(&joint, &info));
        CHECK(info.borrowed[FREE] == TEST_SECTOR && info.available[DATA] == 36);
        if(scenario == 3) {
            kosdec_adx_joint_reader_cancel(&reader);
            CHECK(kosdec_adx_joint_reader_read(&reader, pcm, 128).status == KOSDEC_ADX_CANCELLED);
        }
        else {
            file.fail = true;
            CHECK(kosdec_adx_joint_reader_read(&reader, pcm, 128).status == KOSDEC_ADX_IO_ERROR);
        }
        unsigned calls = file.calls;
        OK(kosdec_adx_joint_reader_close(&reader));
        OK(kosdec_adx_joint_query(&joint, &info));
        CHECK(!info.borrowed[FREE] && info.available[DATA] == 36 && file.calls == calls);
        OK(kosdec_adx_joint_close(&joint));
        ++cases;
        return;
    }

    if(scenario == 0)
        CHECK(kosdec_adx_joint_reader_release_loop(&reader) == KOSDEC_ADX_MORE);
    prefetch(&file, &reader, 2 * TEST_SECTOR - FILE_HEADER);
    if(scenario == 1)
        CHECK(kosdec_adx_joint_reader_release_loop(&reader) == KOSDEC_ADX_MORE);
    if(scenario == 2) prefetch(&file, &reader, 3 * TEST_SECTOR - FILE_HEADER);
    struct kosdec_adx_joint_read_result result = kosdec_adx_joint_reader_read(&reader, pcm, 128);
    CHECK(result.status == KOSDEC_ADX_MORE);
    if(scenario == 0) {
        CHECK(result.frames == 127 && digest_pcm(pcm, 127) == 0xf1f6837f);
        CHECK(reader.consumed_bytes == FILE_HEADER + FILE_BODY &&
              !reader.job.wraps && !reader.job.trap);
    }
    else {
        CHECK(result.frames == end && digest_pcm(pcm, end) ==
              (loop ? loop->first_digest : 0x38494dfd));
        CHECK(reader.consumed_bytes == FILE_HEADER + end_bytes &&
              reader.decoder.frames_done == start);
        CHECK(joint.position[DATA] == 2 * TEST_SECTOR && reader.job.wraps == 1);
        if(scenario == 2) {
            result = kosdec_adx_joint_reader_read(&reader, pcm, 128);
            CHECK(result.status == KOSDEC_ADX_MORE && result.frames == end - start);
            CHECK(digest_pcm(pcm, result.frames) == (loop ? loop->second_digest : 0x9ee7542f));
            CHECK(reader.job.wraps == 2 && reader.decoder.frames_done == start &&
                  joint.position[DATA] == 3 * TEST_SECTOR);
        }
        else {
            prefetch(&file, &reader, TEST_SECTOR);
            result = kosdec_adx_joint_reader_read(&reader, pcm, 128);
            CHECK(result.frames == 127 - start && digest_pcm(pcm, result.frames) ==
                  (loop ? loop->second_digest : 0xaad9d17a));
            CHECK(!reader.job.trap && reader.job.wraps == 1 &&
                  reader.consumed_bytes == FILE_HEADER + end_bytes + 108);
        }
    }
    if(scenario < 2)
        CHECK(kosdec_adx_joint_reader_read(&reader, pcm, 128).status == KOSDEC_ADX_DONE);
    OK(kosdec_adx_joint_reader_close(&reader));
    OK(kosdec_adx_joint_close(&joint));
    ++cases;
}
int main(void) {
    const unsigned fragments[]={1,17,63,512,4096};
    controls(); late_tail();
    jobs(); job_pipe(false); job_pipe(true);
    queue_jobs(); queue_node_exhaustion();
    for(unsigned i = 0; i < 6; ++i) sector_jobs(i, TEST_SECTOR, KOSDEC_ADX_MORE, NULL);
    sector_jobs(1, 2056, KOSDEC_ADX_MORE, NULL);
    sector_jobs(2, 2049, KOSDEC_ADX_MORE, NULL);
    sector_jobs(2, 2056, KOSDEC_ADX_MORE, NULL);
    sector_jobs(2, 2083, KOSDEC_ADX_MORE, NULL);
    sector_jobs(2, 2084, KOSDEC_ADX_MORE, NULL);
    sector_jobs(2, 2011, KOSDEC_ADX_UNSUPPORTED, NULL);
    sector_jobs(2, 4096, KOSDEC_ADX_UNSUPPORTED, NULL);
    /* Independent SDK stream trap and post-seek release witnesses. */
    static const struct logical_loop loops[] = {
        {33,63,0x38494dfd,0x0cb123a3},
        {33,64,0x0270e257,0x9ee7542f},
        {47,95,0x459cc6ae,0x38bf042f},
        {33,63,0x38494dfd,0x95e59010},
        {33,65,0,0},
        {63,95,0,0},
    };
    for(unsigned i = 0; i < 3; ++i) sector_jobs(2, 2056, KOSDEC_ADX_MORE, &loops[i]);
    sector_jobs(1, 2056, KOSDEC_ADX_MORE, &loops[3]);
    sector_jobs(2, 2056, KOSDEC_ADX_UNSUPPORTED, &loops[4]);
    sector_jobs(2, 2056, KOSDEC_ADX_UNSUPPORTED, &loops[5]);
    for(unsigned kind=0;kind<3;++kind)for(unsigned fault=0;fault<5;++fault) endings(kind,fault);
    for(unsigned kind=0;kind<4;++kind)for(unsigned ch=1;ch<=2;++ch)
        for(unsigned large=0;large<2;++large)for(unsigned f=0;f<5;++f)
            playback(kind,ch,large ? 1042 : 50,large ? 1042 : 18,fragments[f],f%2);
    printf("joint_reader: %u cases, %u checks passed\n",cases,checks);
    return 0;
}
