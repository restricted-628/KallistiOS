/* Adapter fault/ownership tests. Actual VFS detach semantics are tested in
   utils/vm-test, not established by the backend model below. */
#include <kosdec/adx_named_file.h>
#include <kos/fs.h>
#include <kos/thread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"NAMED_FAIL %u\n",(unsigned)__LINE__); abort(); } } while(0)
static unsigned checks, opens, closes, allocations, frees;
static kthread_t threads[2];
static kthread_t *current = &threads[0];
static uint32_t sr;
static int irq, open_error, size_error, close_error;
static bool alloc_error, live, reused, reenter;
static uint8_t *asset;
static size_t length, cursor;
static int64_t reported_size;
static struct kosdec_adx_named_file *closing_alias;
kthread_t *thd_get_current(void) { return current; }
int irq_inside_int(void) { return irq; }
uint32_t kosdec_adx_named_test_sr(void) { return sr; }
void *named_test_calloc(size_t n, size_t bytes) {
    if(alloc_error) { errno=ENOMEM; return NULL; }
    ++allocations; return calloc(n,bytes);
}
void named_test_free(void *p) { ++frees; free(p); errno=EDOM; }
file_t fs_open(const char *path,int mode) {
    CHECK(!strcmp(path,"/cd/music.adx") && mode==O_RDONLY && !live);
    ++opens;
    if(open_error) { errno=open_error; return FILEHND_INVALID; }
    live=true; cursor=0; return 7;
}
int fs_close(file_t fd) {
    CHECK(fd==7 && live && !reused); ++closes; live=false;
    if(reenter) {
        int cleanup=123;
        CHECK(kosdec_adx_named_file_close(&closing_alias,&cleanup)<0 && errno==EBUSY);
        CHECK(closing_alias && cleanup==123);
    }
    if(close_error) { reused=true; errno=close_error; return -1; }
    return 0;
}
int64_t fs_total64(file_t fd) {
    CHECK(fd==7 && live);
    if(size_error) { errno=size_error; return -1; }
    return reported_size;
}
_off64_t fs_seek64(file_t fd,_off64_t off,int whence) {
    CHECK(fd==7 && live && whence==SEEK_SET && off>=0 && (uint64_t)off<=length);
    cursor=(size_t)off; return off;
}
ssize_t fs_read(file_t fd,void *dst,size_t bytes) {
    CHECK(fd==7 && live);
    if(bytes>length-cursor) bytes=length-cursor;
    memcpy(dst,asset+cursor,bytes); cursor+=bytes; return (ssize_t)bytes;
}
int main(void) {
    asset=stream(50,18,2,32,61,&length); reported_size=(int64_t)length;
    struct kosdec_adx_named_file *f=NULL;
    struct kosdec_adx_stream *loan=NULL,*second=NULL;
    uint8_t cache[512]; int cleanup=123;
    /* Every rejection precedes allocation/open and preserves outputs. */
    const uint32_t masks[]={0x10,0x80,0xf0,0x10000000};
    for(unsigned i=0;i<sizeof(masks)/sizeof(*masks);++i) {
        sr=masks[i];
        CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==EPERM);
        CHECK(!f && cleanup==123 && !opens);
    }
    sr=0; irq=1;
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==EPERM);
    irq=0; current=NULL;
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==EPERM);
    current=&threads[0];
    CHECK(kosdec_adx_named_file_open(&f,"",false,NULL,0,&cleanup)<0 && errno==EINVAL);
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,1,&cleanup)<0 && errno==EINVAL);
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,cache,511,&cleanup)<0 && errno==EINVAL);
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,cache,32769,&cleanup)<0 && errno==EINVAL);
    CHECK(cleanup==123 && !f && !opens && !allocations);
    alloc_error=true;
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==ENOMEM);
    CHECK(cleanup==0 && !opens && !f); alloc_error=false;
    open_error=ENOENT;
    CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==ENOENT);
    CHECK(!f && !closes && cleanup==0 && allocations==frees); open_error=0;
    for(unsigned mode=0;mode<4;++mode) {
        size_error=mode&1?0:ESPIPE;
        reported_size=mode&1?(int64_t)UINT32_MAX+1:(int64_t)length;
        close_error=mode&2?EIO:0;
        unsigned before=closes;
        CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,cache,sizeof(cache),&cleanup)<0);
        CHECK(errno==(size_error?ESPIPE:EOVERFLOW));
        CHECK(!f && cleanup==close_error && closes==before+1 && !live && allocations==frees);
        reused=false;
    }
    size_error=close_error=0; reported_size=(int64_t)length;
    for(unsigned mode=0;mode<2;++mode) {
        CHECK(!kosdec_adx_named_file_open(&f,"/cd/music.adx",false,mode?cache:NULL,mode?sizeof(cache):0,&cleanup));
        CHECK(f && !cleanup);
        cleanup=123;
        CHECK(kosdec_adx_named_file_open(&f,"/cd/music.adx",false,NULL,0,&cleanup)<0 && errno==EINVAL && cleanup==123);
        current=&threads[1];
        CHECK(kosdec_adx_named_file_borrow(f,&loan)<0 && errno==EPERM && !loan);
        CHECK(kosdec_adx_named_file_close(&f,&cleanup)<0 && errno==EPERM && f && cleanup==123);
        current=&threads[0];
        CHECK(!kosdec_adx_named_file_borrow(f,&loan) && loan);
        CHECK(kosdec_adx_named_file_borrow(f,&second)<0 && errno==EBUSY && !second);
        CHECK(kosdec_adx_named_file_return(f,&second)<0 && errno==EINVAL);
        unsigned before=closes;
        CHECK(kosdec_adx_named_file_close(&f,&cleanup)<0 && errno==EBUSY && f && cleanup==123 && closes==before);
        size_t frames=0;
        for(unsigned i=0;i<100;++i) {
            int16_t pcm[64]; struct kosdec_adx_stream_result got=kosdec_adx_stream_read(loan,pcm,32);
            frames+=got.frames;
            if(got.status>=KOSDEC_ADX_DONE) { CHECK(got.status==KOSDEC_ADX_DONE); break; }
        }
        CHECK(frames==65);
        CHECK(kosdec_adx_named_file_close(&f,&cleanup)<0 && errno==EBUSY); /* EOF is not return. */
        current=&threads[1];
        CHECK(kosdec_adx_named_file_return(f,&loan)<0 && errno==EPERM && loan);
        current=&threads[0]; sr=0xf0;
        CHECK(kosdec_adx_named_file_return(f,&loan)<0 && errno==EPERM && loan);
        sr=0; CHECK(!kosdec_adx_named_file_return(f,&loan) && !loan);
        CHECK(kosdec_adx_named_file_return(f,&loan)<0 && errno==EINVAL);
        CHECK(!kosdec_adx_named_file_borrow(f,&loan));
        CHECK(loan->terminal==KOSDEC_ADX_DONE);
        CHECK(!kosdec_adx_named_file_return(f,&loan));
        close_error=mode?EIO:0; closing_alias=f; reenter=true;
        CHECK(!kosdec_adx_named_file_close(&f,&cleanup));
        CHECK(!f && cleanup==close_error && closes==before+1 && allocations==frees);
        reenter=false; closing_alias=NULL;
        CHECK(kosdec_adx_named_file_close(&f,&cleanup)<0 && errno==EINVAL && closes==before+1);
        reused=false;
    }
    for(unsigned mode=0;mode<2;++mode) {
        struct kosdec_adx_named_file *f=NULL;
        struct kosdec_adx_source loan={0},other={0};
        struct kosdec_adx_stream *stream_loan=NULL;
        int cleanup=0;
        close_error=0;reused=false;
        uint8_t cache[512];
        CHECK(!kosdec_adx_named_file_open(&f,"/cd/music.adx",false,
                                        mode?cache:NULL,mode?sizeof(cache):0,&cleanup));
        CHECK(!kosdec_adx_named_file_borrow_source(f,&loan,42));
        CHECK(loan.bytes==length && loan.tag==42);
        CHECK(kosdec_adx_named_file_borrow(f,&stream_loan)<0 && errno==EBUSY);
        CHECK(kosdec_adx_named_file_borrow_source(f,&other,0)<0 && errno==EBUSY);
        CHECK(kosdec_adx_named_file_close(&f,&cleanup)<0 && errno==EBUSY && f);
        other=loan;
        CHECK(kosdec_adx_named_file_return_source(f,&other)<0 && errno==EINVAL);
        uint8_t bytes[1024];
        struct kosdec_adx_input_result got=loan.read_at(loan.context,0,bytes,sizeof(bytes));
        CHECK(got.status==KOSDEC_ADX_INPUT_DATA && got.bytes<=512 && !memcmp(bytes,asset,got.bytes));
        CHECK(loan.read_at(loan.context,loan.bytes,bytes,1).status==KOSDEC_ADX_INPUT_END);
        CHECK(!kosdec_adx_named_file_return_source(f,&loan) && !loan.read_at);
        CHECK(other.read_at(other.context,0,bytes,1).status==KOSDEC_ADX_INPUT_ERROR);
        CHECK(!kosdec_adx_named_file_borrow(f,&stream_loan));
        CHECK(kosdec_adx_named_file_borrow_source(f,&loan,0)<0 && errno==EBUSY);
        CHECK(!kosdec_adx_named_file_return(f,&stream_loan));
        CHECK(!kosdec_adx_named_file_close(&f,&cleanup) && !f && !cleanup);
    }
    free(asset);
    printf("ADX_NAMED_FILE checks=%u failures=0\n",checks);
    return 0;
}
