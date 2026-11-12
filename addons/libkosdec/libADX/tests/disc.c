/* Original adversarial async contract model. Does not establish DMA execution. */
#include <kosdec/adx_disc.h>
#include <kosdec/adx_index.h>
#include <kosdec/adx_sequence.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "memory_vectors.h"
#include "profile_vectors.h"
#include "stream_fixture.h"
static unsigned failures, cases, submits, polls, destroys, cancels;
#define CHECK(x) do { if(!(x)) { ++failures; printf("DISC_FAIL line=%u\n", (unsigned)__LINE__); if(failures>20) exit(1); } } while(0)
struct cdrom_sector_range { uint8_t *data; size_t sectors, position; cdrom_request_backend_t backend; };
struct cdrom_request {
    cdrom_sector_range_t *range;
    uint8_t *buffer;
    size_t start, bytes;
    unsigned ticks, retire_delay;
    bool cancel;
    cdrom_request_status_t status;
};
static struct cdrom_request request;
static bool active;
static unsigned fault;
/* Faults: 1 seek error, 2 wrong seek, 3 submit error, 4 short success,
   5 error with prefix, 6 timeout, 7 wrong backend, 8 status failure,
   9 destroy failure, 10 cancel failure. */
static _Alignas(32) uint8_t staging[32768];
int cdrom_sector_range_get_info(cdrom_sector_range_t *r, cdrom_sector_range_info_t *s) {
    *s=(cdrom_sector_range_info_t){.backend=r->backend,.start_fad=150,.sector_count=r->sectors,.position=r->position};
    return 0;
}
int64_t cdrom_sector_range_seek(cdrom_sector_range_t *r,int64_t offset,int whence) {
    CHECK(!active && whence==SEEK_SET && offset>=0 && (uint64_t)offset<r->sectors);
    if(fault==1){errno=EBUSY;return -1;}
    if(fault==2)return offset+1;
    r->position=(size_t)offset;return offset;
}
cdrom_request_t *cdrom_sector_range_read_async(cdrom_sector_range_t *r,void *dst,size_t sectors,uint32_t timeout,cdrom_request_callback_t callback,void *data) {
    ++submits;
    CHECK(!active && dst==staging && !((uintptr_t)dst&31u));
    CHECK(sectors && sectors<=16 && sectors<=r->sectors-r->position && timeout==123 && !callback && !data);
    if(fault==3){errno=ENOMEM;return NULL;}
    request=(struct cdrom_request){.range=r,.buffer=dst,.start=r->position,.bytes=sectors*2048,.retire_delay=2,
        .status={.backend=r->backend,.state=CDROM_REQUEST_QUEUED}};
    active=true;return &request;
}
int cdrom_request_get_status(const cdrom_request_t *p,cdrom_request_status_t *s) {
    ++polls;CHECK(active && p==&request);
    if(fault==8){fault=0;errno=EIO;return -1;}
    *s=p->status;return 0;
}
int cdrom_request_destroy(cdrom_request_t *p) {
    ++destroys;CHECK(active && p==&request && p->status.state>=CDROM_REQUEST_COMPLETE);
    if(fault==9){fault=0;errno=EIO;return -1;}
    if(p->retire_delay){--p->retire_delay;errno=EBUSY;return -1;}
    active=false;return 0;
}
int cdrom_request_cancel(cdrom_request_t *p) {
    ++cancels;CHECK(active && p==&request);
    if(fault==10){fault=0;errno=EIO;return -1;}
    p->cancel=true;return 0;
}
/* Device makes progress independently, and writes only its retained staging. */
static void tick(void) {
    if(!active || request.status.state>=CDROM_REQUEST_COMPLETE)return;
    if(++request.ticks<3){request.status.state=CDROM_REQUEST_RUNNING;return;}
    memcpy(request.buffer,request.range->data+request.start*2048,request.bytes);
    request.status.data_bytes=request.bytes;
    request.status.completed_bytes=request.bytes;
    request.status.state=CDROM_REQUEST_COMPLETE;
    if(request.cancel)request.status.state=CDROM_REQUEST_CANCELLED;
    if(fault==4)request.status.completed_bytes--;
    if(fault==5){request.status.state=CDROM_REQUEST_ERROR;request.status.error=EIO;request.status.completed_bytes/=2;}
    if(fault==6){request.status.state=CDROM_REQUEST_TIMED_OUT;request.status.error=ETIMEDOUT;}
    if(fault==7)request.status.backend=CDROM_REQUEST_BACKEND_BIOS;
    if(request.status.state==CDROM_REQUEST_COMPLETE)request.range->position=request.start+request.bytes/2048;
}
static void close_binding(struct kosdec_adx_disc *d) {
    enum kosdec_adx_status s=KOSDEC_ADX_MORE;
    for(unsigned i=0;i<30;++i){s=kosdec_adx_disc_close(d);tick();if(s==KOSDEC_ADX_DONE)break;}
    CHECK(s==KOSDEC_ADX_DONE && !active && !d->request);
    unsigned before=cancels+polls+destroys;
    CHECK(kosdec_adx_disc_close(d)==KOSDEC_ADX_DONE && before==cancels+polls+destroys);
}
static void field(uint8_t *p,uint32_t n,bool afs) {
    if(!afs)put32(p,n);
    else for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));
}
static void playback(void) {
    const unsigned offsets[]={0,31,2047}, buffers[]={2048,4096,32768}, caps[]={1,7,32};
    for(unsigned afs=0;afs<2;++afs)for(unsigned ch=1;ch<=2;++ch)
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)for(unsigned c=0;c<3;++c)
    for(unsigned pad=0;pad<2;++pad) {
        ++cases;
        size_t bytes;uint8_t *p=stream(pad?4097:50,pad?4099:18,ch,32,61,&bytes);
        size_t member=2047, source_bytes=member+bytes+39;
        size_t total=(offsets[a]+source_bytes+2047)/2048*2048;
        uint8_t *raw=calloc(total,1);CHECK(raw!=NULL);uint8_t *base=raw+offsets[a];
        if(afs)memcpy(base,"AFS\0",4);
        field(base+4,1,afs);field(base+8,(uint32_t)member,afs);field(base+12,(uint32_t)bytes,afs);
        memcpy(base+member,p,bytes);memset(base+member+bytes,0xa5,39);free(p);
        struct cdrom_sector_range range={raw,total/2048,0,CDROM_REQUEST_BACKEND_DIRECT};
        struct kosdec_adx_disc d={0};
        CHECK(kosdec_adx_disc_init(&d,&range,offsets[a],(uint32_t)source_bytes,staging,buffers[b],123)==KOSDEC_ADX_MORE);
        struct kosdec_adx_index x;kosdec_adx_index_init(&x,kosdec_adx_disc_read_at,&d,(uint32_t)source_bytes,afs?KOSDEC_ADX_AFS:KOSDEC_ADX_ACX,0);
        enum kosdec_adx_status status=KOSDEC_ADX_MORE;
        for(unsigned i=0;i<1000;++i){status=kosdec_adx_index_step(&x);tick();if(status>=KOSDEC_ADX_DONE)break;}
        CHECK(status==KOSDEC_ADX_DONE);
        struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,kosdec_adx_view_read_at,&x.member,x.member.bytes,true);
        size_t at=0;
        for(unsigned i=0;i<3000;++i) {
            if(s.wraps==2)kosdec_adx_stream_release_loop(&s);
            int16_t pcm[64];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&s,pcm,caps[c]);
            for(size_t j=0;j<z.frames;++j)for(unsigned k=0;k<ch;++k){CHECK(at+j<123);if(at+j<123)CHECK(pcm[j*ch+k]==vectors[4].pcm[k][at+j]);}
            at+=z.frames;tick();status=z.status;if(status>=KOSDEC_ADX_DONE)break;
        }
        CHECK(status==KOSDEC_ADX_DONE && at==123 && s.wraps==2 && !d.io_errno);
        close_binding(&d);free(raw);
    }
}
static void lifecycle(void) {
    uint8_t *raw=malloc(40*2048);CHECK(raw!=NULL);
    for(size_t i=0;i<40*2048;++i)raw[i]=(uint8_t)(i*7+i/2048);
    struct cdrom_sector_range range={raw,40,0,CDROM_REQUEST_BACKEND_DIRECT};
    struct kosdec_adx_disc d={0};uint8_t dst[1024];
    CHECK(kosdec_adx_disc_init(&d,&range,17,40000,staging,32768,123)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_disc_init(&d,&range,17,40000,staging,32768,123)==KOSDEC_ADX_BAD_ARGUMENT);
    memset(dst,0xa5,sizeof(dst));
    struct kosdec_adx_input_result r=kosdec_adx_disc_read_at(&d,0,dst,0);
    CHECK(r.status==KOSDEC_ADX_INPUT_WAIT && !active);
    unsigned before=submits;
    r=kosdec_adx_disc_read_at(&d,0,dst,sizeof(dst));
    CHECK(r.status==KOSDEC_ADX_INPUT_WAIT && active && submits==before+1);
    /* A live DMA source cannot retain the caller's transient destination. */
    for(unsigned i=0;i<4;++i) {
        tick();r=kosdec_adx_disc_read_at(&d,0,dst,sizeof(dst));
        CHECK(r.status==KOSDEC_ADX_INPUT_WAIT && !r.bytes && active);
        for(size_t j=0;j<sizeof(dst);++j)CHECK(dst[j]==0xa5);
    }
    r=kosdec_adx_disc_read_at(&d,0,dst,sizeof(dst));
    CHECK(r.status==KOSDEC_ADX_INPUT_DATA && r.bytes==512 && !active && !memcmp(dst,raw+17,512));
    for(unsigned i=512;i<32000;i+=512) {
        r=kosdec_adx_disc_read_at(&d,i,dst,sizeof(dst));
        CHECK(r.status==KOSDEC_ADX_INPUT_DATA && !memcmp(dst,raw+17+i,r.bytes));
    }
    CHECK(submits==before+1); /* read-ahead amortizes all these provider calls */
    r=kosdec_adx_disc_read_at(&d,39999,dst,sizeof(dst));CHECK(r.status==KOSDEC_ADX_INPUT_WAIT);
    /* Offset changes wait for retirement, then request their own cache later. */
    unsigned submission=submits;
    for(unsigned i=0;i<10 && active;++i){tick();r=kosdec_adx_disc_read_at(&d,0,dst,7);CHECK(r.status==KOSDEC_ADX_INPUT_WAIT);}
    CHECK(!active && submits==submission);
    r=kosdec_adx_disc_read_at(&d,39999,dst,sizeof(dst));
    CHECK(r.status==KOSDEC_ADX_INPUT_DATA && r.bytes==1 && dst[0]==raw[40016]);
    CHECK(kosdec_adx_disc_read_at(&d,40000,dst,sizeof(dst)).status==KOSDEC_ADX_INPUT_END);
    CHECK(kosdec_adx_disc_read_at(&d,40001,dst,sizeof(dst)).status==KOSDEC_ADX_INPUT_ERROR);
    close_binding(&d);
    CHECK(kosdec_adx_disc_read_at(&d,0,dst,1).status==KOSDEC_ADX_INPUT_ERROR);
    /* Cancel in queued/running/terminal-but-not-retired states. */
    for(unsigned ticks=0;ticks<4;++ticks) {
        CHECK(kosdec_adx_disc_init(&d,&range,0,40000,staging,2048,123)==KOSDEC_ADX_MORE);
        CHECK(kosdec_adx_disc_read_at(&d,0,dst,1).status==KOSDEC_ADX_INPUT_WAIT);
        for(unsigned j=0;j<ticks;++j)tick();
        CHECK(kosdec_adx_disc_close(&d)==KOSDEC_ADX_NEED_INPUT && active && !d.closed);
        close_binding(&d);
    }
    for(unsigned f=1;f<=10;++f) {
        fault=f;
        CHECK(kosdec_adx_disc_init(&d,&range,0,40000,staging,2048,123)==KOSDEC_ADX_MORE);
        memset(dst,0xa5,sizeof(dst));
        r=kosdec_adx_disc_read_at(&d,0,dst,17);
        if(f==10){CHECK(kosdec_adx_disc_close(&d)==KOSDEC_ADX_NEED_INPUT);close_binding(&d);CHECK(d.io_errno==EIO);continue;}
        for(unsigned i=0;i<20 && r.status==KOSDEC_ADX_INPUT_WAIT;++i){tick();r=kosdec_adx_disc_read_at(&d,0,dst,17);}
        CHECK(r.status==KOSDEC_ADX_INPUT_ERROR && !r.bytes && d.io_errno);
        for(size_t i=0;i<sizeof(dst);++i)CHECK(dst[i]==0xa5);
        before=submits+polls;CHECK(kosdec_adx_disc_read_at(&d,0,dst,17).status==KOSDEC_ADX_INPUT_ERROR && before==submits+polls);
        fault=0;close_binding(&d);
    }
    CHECK(kosdec_adx_disc_init(&d,&range,0,10,staging+1,2048,123)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_disc_init(&d,&range,0,10,staging,2049,123)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_disc_init(&d,&range,0,10,staging,2048,0)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_disc_init(&d,&range,UINT64_MAX,10,staging,2048,123)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_disc_init(&d,&range,40*2048-1,2,staging,2048,123)==KOSDEC_ADX_BAD_ARGUMENT);
    range.backend=CDROM_REQUEST_BACKEND_BIOS;
    CHECK(kosdec_adx_disc_init(&d,&range,0,10,staging,2048,123)==KOSDEC_ADX_UNSUPPORTED);
    CHECK(kosdec_adx_disc_close(NULL)==KOSDEC_ADX_BAD_ARGUMENT);
    free(raw);
}
static void sequence_playback(void) {
    size_t bytes;uint8_t *p=stream(4097,4099,2,0,1,&bytes);
    memset(p+20,0,24);
    size_t sectors=(bytes+2047)/2048;
    uint8_t *raw=calloc(sectors,2048);CHECK(raw!=NULL);memcpy(raw,p,bytes);free(p);
    struct cdrom_sector_range range={raw,sectors,0,CDROM_REQUEST_BACKEND_DIRECT};
    struct kosdec_adx_disc d={0};
    CHECK(kosdec_adx_disc_init(&d,&range,0,(uint32_t)bytes,staging,2048,123)==KOSDEC_ADX_MORE);
    struct kosdec_adx_sequence s;kosdec_adx_sequence_init(&s,false);
    for(unsigned i=0;i<3;++i) {
        struct kosdec_adx_source a={kosdec_adx_disc_read_at,&d,(uint32_t)bytes,i};
        CHECK(kosdec_adx_sequence_enqueue(&s,&a)==KOSDEC_ADX_MORE);
    }
    CHECK(kosdec_adx_sequence_start(&s)==KOSDEC_ADX_MORE);
    enum kosdec_adx_status status=KOSDEC_ADX_MORE;unsigned total=0;
    for(unsigned i=0;i<5000;++i) {
        int16_t pcm[14];struct kosdec_adx_sequence_result r=kosdec_adx_sequence_read(&s,pcm,7);
        for(size_t j=0;j<r.frames;++j)for(unsigned k=0;k<2;++k)CHECK(pcm[j*2+k]==golden[k][r.source_frame+j]);
        total+=(unsigned)r.frames;tick();status=r.status;if(status>=KOSDEC_ADX_DONE)break;
    }
    CHECK(status==KOSDEC_ADX_DONE && total==195 && s.completed_entries==3);
    close_binding(&d);free(raw);
}
static void seek_with_pending_io(void) {
    size_t bytes;uint8_t *data=stream(4097,4099,2,32,61,&bytes);
    size_t sectors=(bytes+2047)/2048;uint8_t *raw=calloc(sectors,2048);CHECK(raw!=NULL);
    memcpy(raw,data,bytes);free(data);
    struct cdrom_sector_range range={raw,sectors,0,CDROM_REQUEST_BACKEND_DIRECT};
    struct kosdec_adx_disc d={0};
    CHECK(kosdec_adx_disc_init(&d,&range,0,(uint32_t)bytes,staging,2048,123)==KOSDEC_ADX_MORE);
    struct kosdec_adx_stream s;kosdec_adx_stream_init(&s,kosdec_adx_disc_read_at,&d,(uint32_t)bytes,false);
    for(unsigned n=0;n<5000;++n) {
        int16_t pcm[64];CHECK(kosdec_adx_stream_read(&s,pcm,32).status<KOSDEC_ADX_DONE);
        if(s.source_frame==65 && active)break;
        tick();
    }
    CHECK(active && s.source_frame==65);
    cdrom_request_t *pending=d.request;
    CHECK(kosdec_adx_stream_seek_begin(&s,33)==KOSDEC_ADX_MORE && d.request==pending && active);
    enum kosdec_adx_status status=KOSDEC_ADX_MORE;
    for(unsigned n=0;n<5000;++n) {
        status=kosdec_adx_stream_seek_step(&s);tick();
        if(status>=KOSDEC_ADX_DONE)break;
    }
    CHECK(status==KOSDEC_ADX_DONE && s.source_frame==33 && !s.returned_frames);
    unsigned at=33;
    for(unsigned n=0;n<5000;++n) {
        int16_t pcm[64];struct kosdec_adx_stream_result got=kosdec_adx_stream_read(&s,pcm,32);
        for(size_t j=0;j<got.frames;++j)for(unsigned c=0;c<2;++c)CHECK(pcm[j*2+c]==golden[c][at+j]);
        at+=(unsigned)got.frames;status=got.status;tick();if(status>=KOSDEC_ADX_DONE)break;
    }
    CHECK(status==KOSDEC_ADX_DONE && at==65 && s.returned_frames==32);
    close_binding(&d);free(raw);
}
int main(void) {
    playback();lifecycle();sequence_playback();seek_with_pending_io();
    printf("ADX_DISC failures=%u PCM_cases=%u submits=%u polls=%u destroys=%u cancels=%u\n",failures,cases,submits,polls,destroys,cancels);
    return failures?1:0;
}
