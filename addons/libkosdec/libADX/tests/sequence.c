/* Original ordered PCM/queue regressions with independent rational vectors. */
#include <kosdec/adx_sequence.h>
#include <kosdec/adx_index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#include "profile_vectors.h"
#include "rate_vectors.h"
static unsigned failures,cases;
#define CHECK(x) do { if(!(x)){++failures;printf("SEQUENCE_FAIL line=%u\n",(unsigned)__LINE__);if(failures>20)exit(1);} } while(0)
struct input { uint8_t *data;uint32_t bytes;unsigned calls,fragment,wait,fault; };
static struct kosdec_adx_input_result provider(void *ctx,uint32_t off,uint8_t *dst,size_t cap) {
    struct input *p=ctx;++p->calls;CHECK(off<p->bytes && cap<=p->bytes-off && cap<=512);
    if(p->fault)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR,0};
    if(p->wait && p->calls%3==0)return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT,0};
    if(cap>p->fragment)cap=p->fragment;
    memcpy(dst,p->data+off,cap);return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA,cap};
}
static struct input fixture(unsigned frames,unsigned ch,unsigned rate,bool pad) {
    unsigned header=pad?1025:50,tail=pad?1027:18;size_t size;
    uint8_t *p=stream(header,tail,ch,0,1,&size);
    memset(p+20,0,24);put32(p+8,rate);put32(p+12,frames);
    size_t end=header+(frames+31)/32*18*ch;
    memmove(p+end,p+size-tail,tail);
    return (struct input){p,(uint32_t)(end+tail),0,512,0,0};
}
static struct kosdec_adx_source source(struct input *p,uint32_t tag) {
    return (struct kosdec_adx_source){provider,p,p->bytes,tag};
}
static unsigned calls(struct input *p,unsigned n) { unsigned v=0;while(n--)v+=p[n].calls;return v; }
static void combinations(void) {
    const unsigned counts[]={1,33,65,0},frags[]={1,7,512},caps[]={1,7,64};
    for(unsigned ch=1;ch<=2;++ch)for(unsigned rate=0;rate<2;++rate)
    for(unsigned f=0;f<3;++f)for(unsigned c=0;c<3;++c)
    for(unsigned repeat=0;repeat<2;++repeat)for(unsigned pad=0;pad<2;++pad)
    for(unsigned wait=0;wait<2;++wait) {
        ++cases;struct input p[4];struct kosdec_adx_sequence s;
        kosdec_adx_sequence_init(&s,repeat);
        for(unsigned j=0;j<4;++j) {
            p[j]=fixture(counts[j],ch,rate?22050:44100,pad);p[j].fragment=frags[f];p[j].wait=wait;
            p[j].data[18]=j%2?5:3;
            struct kosdec_adx_source a=source(&p[j],10+j);
            CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);
        }
        CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_MORE);
        uint64_t total=0;enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned step=0;step<100000;++step) {
            if(repeat && s.completed_entries>=8)kosdec_adx_sequence_repeat(&s,false);
            int16_t pcm[130];for(unsigned j=0;j<130;++j)pcm[j]=12345;
            unsigned before=calls(p,4);uint64_t completed=s.completed_entries;
            if(step%19==0) {
                kosdec_adx_sequence_pause(&s,true);
                CHECK(kosdec_adx_sequence_read(&s,pcm,caps[c]).status==KOSDEC_ADX_MORE && calls(p,4)==before);
                kosdec_adx_sequence_pause(&s,false);
                CHECK(kosdec_adx_sequence_read(&s,NULL,0).status==KOSDEC_ADX_NEED_OUTPUT && calls(p,4)==before);
            }
            struct kosdec_adx_sequence_result r=kosdec_adx_sequence_read(&s,pcm,caps[c]);
            CHECK(calls(p,4)<=before+1 && s.completed_entries<=completed+1 && r.frames<=32 && r.frames<=caps[c]);
            CHECK(r.tag==10+completed%4);
            for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<ch;++k) {
                CHECK(r.source_frame+j<counts[completed%4]);
                if(r.source_frame+j<65)CHECK(pcm[j*ch+k]==(rate?rate_vectors[5].pcm[k][r.source_frame+j]:golden[k][r.source_frame+j]));
            }
            for(size_t j=r.frames*ch;j<130;++j)CHECK(pcm[j]==12345);
            if(r.frames)CHECK(s.format_ready && s.channels==ch && s.sample_rate==(rate?22050u:44100u));
            total+=r.frames;status=r.status;if(status>=KOSDEC_ADX_DONE)break;
        }
        CHECK(status==KOSDEC_ADX_DONE && total==99*(repeat?3u:1u) && s.returned_frames==total);
        CHECK(s.completed_entries==(repeat?12u:4u) && s.requeues==(repeat?8u:0u) && !s.count);
        unsigned before=calls(p,4);kosdec_adx_sequence_cancel(&s);
        CHECK(kosdec_adx_sequence_read(&s,NULL,0).status==KOSDEC_ADX_DONE && calls(p,4)==before);
        for(unsigned j=0;j<4;++j)free(p[j].data);
    }
}
static enum kosdec_adx_status drain(struct kosdec_adx_sequence *s) {
    for(unsigned i=0;i<10000;++i){int16_t out[64];struct kosdec_adx_sequence_result r=kosdec_adx_sequence_read(s,out,32);if(r.status>=KOSDEC_ADX_DONE)return r.status;}
    CHECK(0);return KOSDEC_ADX_INVALID;
}
static void lifecycle(void) {
    struct input p=fixture(65,2,44100,false);struct kosdec_adx_source a=source(&p,77);
    struct kosdec_adx_sequence s,copy;kosdec_adx_sequence_init(&s,false);int16_t pcm[64];
    CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_NEED_INPUT && !s.started);
    CHECK(kosdec_adx_sequence_read(&s,pcm,32).status==KOSDEC_ADX_BAD_ARGUMENT && !p.calls);
    CHECK(kosdec_adx_sequence_enqueue(&s,NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    struct kosdec_adx_source bad=a;bad.bytes=0;
    CHECK(kosdec_adx_sequence_enqueue(&s,&bad)==KOSDEC_ADX_BAD_ARGUMENT);
    for(unsigned i=0;i<16;++i){a.tag=i;CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);}
    copy=s;CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_NEED_OUTPUT && !memcmp(&s,&copy,sizeof(s)));
    CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_MORE);
    copy=s;CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&s,&copy,sizeof(s)));
    CHECK(kosdec_adx_sequence_read(&s,NULL,1).status==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&s,&copy,sizeof(s)));
    CHECK(kosdec_adx_sequence_read(&s,(int16_t *)((uint8_t *)pcm+1),1).status==KOSDEC_ADX_BAD_ARGUMENT && !memcmp(&s,&copy,sizeof(s)));
    while(!s.completed_entries)CHECK(kosdec_adx_sequence_read(&s,pcm,32).status<KOSDEC_ADX_DONE);
    CHECK(s.count==15);a.tag=16;CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);
    uint64_t entries=s.completed_entries;
    for(unsigned i=0;i<5000;++i) {
        struct kosdec_adx_sequence_result r=kosdec_adx_sequence_read(&s,pcm,32);
        CHECK(r.tag==entries);entries=s.completed_entries;if(r.status>=KOSDEC_ADX_DONE){CHECK(r.status==KOSDEC_ADX_DONE);break;}
    }
    CHECK(s.completed_entries==17 && s.returned_frames==17*65);
    CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_DONE);
    /* Cancel is sticky before start and during a partial header. */
    for(unsigned started=0;started<2;++started) {
        kosdec_adx_sequence_init(&s,true);CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);
        if(started){p.fragment=1;kosdec_adx_sequence_start(&s);kosdec_adx_sequence_read(&s,pcm,32);}
        unsigned before=p.calls;kosdec_adx_sequence_cancel(&s);
        CHECK(drain(&s)==KOSDEC_ADX_CANCELLED && p.calls==before);
        CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_CANCELLED);
    }
    /* Reject a second entry before its PCM, after prior validated output. */
    p.fragment=512;
    for(unsigned fault=0;fault<5;++fault) {
        struct input q=fixture(65,2,44100,false);struct kosdec_adx_source b=source(&q,88);
        if(fault==0)put32(q.data+8,22050);
        if(fault==1)q.data[7]=1;
        if(fault==2){put32(q.data+20,1);put32(q.data+24,1);put32(q.data+28,0);put32(q.data+32,50);put32(q.data+36,1);put32(q.data+40,86);}
        if(fault==3)q.fault=1;
        if(fault==4)q.data[0]=0;
        kosdec_adx_sequence_init(&s,false);kosdec_adx_sequence_enqueue(&s,&a);kosdec_adx_sequence_enqueue(&s,&b);kosdec_adx_sequence_start(&s);
        CHECK(drain(&s)==(fault<3?KOSDEC_ADX_UNSUPPORTED:fault==3?KOSDEC_ADX_IO_ERROR:KOSDEC_ADX_INVALID));
        CHECK(s.completed_entries==1 && s.returned_frames==65);
        free(q.data);
    }
    /* A corrupt footer must not start the next file or requeue the old one. */
    struct input q=fixture(1,2,44100,false);struct kosdec_adx_source b=source(&q,88);
    p.data[p.bytes-1]=1;
    kosdec_adx_sequence_init(&s,true);kosdec_adx_sequence_enqueue(&s,&a);kosdec_adx_sequence_enqueue(&s,&b);kosdec_adx_sequence_start(&s);
    CHECK(drain(&s)==KOSDEC_ADX_INVALID && s.returned_frames==65 && !s.completed_entries && !s.requeues && !q.calls);
    free(p.data);free(q.data);
}
static void indexed(void) {
    for(unsigned afs=0;afs<2;++afs) {
        struct input p=fixture(65,2,44100,false);uint32_t bytes=p.bytes;
        uint8_t *raw=calloc(37+bytes,1);CHECK(raw!=NULL);
        if(afs)memcpy(raw,"AFS\0",4);
        uint32_t fields[]={1,37,bytes};
        for(unsigned f=0;f<3;++f)for(unsigned j=0;j<4;++j)raw[4+f*4+j]=(uint8_t)(fields[f]>>(afs?j*8:24-j*8));
        memcpy(raw+37,p.data,bytes);free(p.data);p.data=raw;p.bytes=37+bytes;p.fragment=7;p.wait=1;
        struct kosdec_adx_index x;kosdec_adx_index_init(&x,provider,&p,p.bytes,afs?KOSDEC_ADX_AFS:KOSDEC_ADX_ACX,0);
        enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned i=0;i<100;++i){status=kosdec_adx_index_step(&x);if(status>=KOSDEC_ADX_DONE)break;}
        CHECK(status==KOSDEC_ADX_DONE);
        struct kosdec_adx_sequence s;kosdec_adx_sequence_init(&s,false);
        struct kosdec_adx_source a={kosdec_adx_view_read_at,&x.member,x.member.bytes,9};
        for(unsigned i=0;i<3;++i)CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);
        kosdec_adx_sequence_start(&s);unsigned frames=0;
        for(unsigned i=0;i<5000;++i) {
            int16_t out[14];struct kosdec_adx_sequence_result r=kosdec_adx_sequence_read(&s,out,7);
            for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<2;++k)CHECK(out[j*2+k]==golden[k][r.source_frame+j]);
            frames+=(unsigned)r.frames;status=r.status;if(status>=KOSDEC_ADX_DONE)break;
        }
        CHECK(status==KOSDEC_ADX_DONE && frames==195 && s.completed_entries==3);free(p.data);
    }
}
int main(void) { combinations();lifecycle();indexed();printf("ADX_SEQUENCE cases=%u failures=%u\n",cases,failures);return failures?1:0; }
