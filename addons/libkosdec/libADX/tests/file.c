/* Compile the real adapter against a host FILE-backed VFS contract fixture. */
#include <kosdec/adx_file.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_vectors.h"
#include "stream_fixture.h"
static unsigned failures,calls,reads;static FILE *handle;
static int fault;static int64_t length;
static size_t read_limit=7,request_limit=512;
#define CHECK(x) do { if(!(x)) { ++failures; printf("FILE_FAIL line=%u\n",(unsigned)__LINE__); } } while(0)
int64_t fs_total64(file_t fd) {
    if(fd!=7){errno=EBADF;return -1;}return length;
}
_off64_t fs_seek64(file_t fd,_off64_t off,int whence) {
    ++calls;CHECK(fd==7 && whence==SEEK_SET);
    if(fault==1){errno=ESPIPE;return -1;}
    if(fault==2)return off+1;
    if(fseeko(handle,(off_t)off,whence)){return -1;}return ftello(handle);
}
ssize_t fs_read(file_t fd,void *dst,size_t bytes) {
    ++reads;CHECK(fd==7 && bytes<=request_limit);
    if(fault==3){errno=EIO;return -1;}
    if(fault==4){fault=0;errno=EINTR;return -1;}
    if(fault==5){fault=0;errno=EAGAIN;return -1;}
    if(fault==6)return 0;
    if(fault==7)return (ssize_t)bytes+1;
    if(bytes>read_limit)bytes=read_limit;
    return (ssize_t)fread(dst,1,bytes,handle);
}
static enum kosdec_adx_status drain(struct kosdec_adx_file *f) {
    for(unsigned i=0;i<1000;++i) {
        int16_t pcm[64];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&f->stream,pcm,32);
        if(z.status>=KOSDEC_ADX_DONE)return z.status;
    }CHECK(0);return KOSDEC_ADX_INVALID;
}
static size_t collect(struct kosdec_adx_file *f,int16_t *out,size_t capacity) {
    size_t frames=0;
    for(unsigned i=0;i<200000;++i) {
        int16_t pcm[64];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&f->stream,pcm,32);
        CHECK(frames+z.frames<=capacity);
        if(frames+z.frames>capacity)return frames;
        memcpy(out+frames*2,pcm,z.frames*4);frames+=z.frames;
        if(z.status>=KOSDEC_ADX_DONE){CHECK(z.status==KOSDEC_ADX_DONE);return frames;}
    }CHECK(0);return frames;
}
static void buffered(void) {
    uint8_t cache[32768];struct kosdec_adx_file f,saved;
    const size_t total=65536,groups=total/32;
    size_t small_bytes;uint8_t *small=stream(50,18,2,32,61,&small_bytes);
    size_t bytes=50+groups*36+18;uint8_t *large=malloc(bytes);
    int16_t *expected=malloc(total*4),*actual=malloc(total*4);
    if(!large || !expected || !actual)exit(2);
    memcpy(large,small,50);memset(large+20,0,24);put32(large+12,(uint32_t)total);
    for(size_t i=0;i<groups;++i)memcpy(large+50+i*36,small+50,36);
    memcpy(large+50+groups*36,small+small_bytes-18,18);free(small);
    fclose(handle);handle=tmpfile();if(!handle)exit(2);
    CHECK(fwrite(large,1,bytes,handle)==bytes);fflush(handle);free(large);length=(int64_t)bytes;
    fault=0;read_limit=32768;
    CHECK(kosdec_adx_file_init(&f,7,false)==KOSDEC_ADX_MORE);
    unsigned before=reads;CHECK(collect(&f,expected,total)==total);unsigned unbuffered=reads-before;
    const size_t capacities[]={512,4096,32768};
    for(unsigned i=0;i<3;++i) {
        request_limit=capacities[i];
        CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,capacities[i])==KOSDEC_ADX_MORE);
        before=reads;CHECK(collect(&f,actual,total)==total);unsigned buffered_reads=reads-before;
        CHECK(!memcmp(actual,expected,total*4));
        CHECK(buffered_reads==(bytes+capacities[i]-1)/capacities[i]);
        printf("ADX_FILE_CACHE capacity=%zu frames=%zu reads=%u baseline=%u\n",capacities[i],total,buffered_reads,unbuffered);
        /* Rebuild history across a backward cache miss and a partial group. */
        CHECK(kosdec_adx_stream_seek_begin(&f.stream,33)==KOSDEC_ADX_MORE);
        enum kosdec_adx_status status;
        do {status=kosdec_adx_stream_seek_step(&f.stream);}while(status<KOSDEC_ADX_DONE);
        CHECK(status==KOSDEC_ADX_DONE);
        CHECK(collect(&f,actual,total)==total-33 && !memcmp(actual,expected+66,(total-33)*4));
    }
    /* Positive short refills and interrupted/would-block refills retain exact PCM. */
    read_limit=17;
    for(int i=4;i<=5;++i) {
        CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,sizeof(cache))==KOSDEC_ADX_MORE);
        fault=i;CHECK(collect(&f,actual,total)==total && !memcmp(actual,expected,total*4));
    }
    read_limit=32768;
    /* Cached bytes bypass I/O; a failed miss invalidates data before retry/seek. */
    CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,512)==KOSDEC_ADX_MORE);
    uint8_t out[512];struct kosdec_adx_input_result got=f.stream.read_at(&f,0,out,8);
    CHECK(got.status==KOSDEC_ADX_INPUT_DATA && got.bytes==8);
    before=reads;fault=3;got=f.stream.read_at(&f,4,out,8);
    CHECK(got.status==KOSDEC_ADX_INPUT_DATA && reads==before);
    got=f.stream.read_at(&f,512,out,8);CHECK(got.status==KOSDEC_ADX_INPUT_ERROR && !f.cache_used);
    fault=0;got=f.stream.read_at(&f,0,out,8);CHECK(got.status==KOSDEC_ADX_INPUT_DATA && reads==before+2);
    before=reads;got=f.stream.read_at(&f,(uint32_t)length,out,8);
    CHECK(got.status==KOSDEC_ADX_INPUT_END && !got.bytes && reads==before);
    for(int i=1;i<=7;++i) {
        fault=i;CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,512)==KOSDEC_ADX_MORE);
        got=f.stream.read_at(&f,0,out,8);
        CHECK(got.status==(i==4||i==5?KOSDEC_ADX_INPUT_WAIT:i==6?KOSDEC_ADX_INPUT_END:KOSDEC_ADX_INPUT_ERROR));
        CHECK(!f.cache_used);
    }
    fault=0;CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,512)==KOSDEC_ADX_MORE);saved=f;before=calls;
    CHECK(kosdec_adx_file_init_buffered(&f,7,false,NULL,512)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,511)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(kosdec_adx_file_init_buffered(&f,7,false,cache,32769)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(!memcmp(&f,&saved,sizeof(f)) && calls==before);
    kosdec_adx_stream_cancel(&f.stream);CHECK(drain(&f)==KOSDEC_ADX_CANCELLED && calls==before);
    CHECK(kosdec_adx_file_init(&f,7,false)==KOSDEC_ADX_MORE && !f.cache && !f.cache_used);
    free(expected);free(actual);
}
int main(void) {
    size_t n;uint8_t *p=stream(50,18,2,32,61,&n);length=(int64_t)n;
    handle=tmpfile();if(!handle)return 2;CHECK(fwrite(p,1,n,handle)==n);fflush(handle);free(p);
    struct kosdec_adx_file f;
    uint8_t loop_cache[512];
    for(unsigned mode=0;mode<3;++mode) {
    read_limit=mode==2?32768:7;
    CHECK((mode?kosdec_adx_file_init_buffered(&f,7,true,loop_cache,sizeof(loop_cache)):
           kosdec_adx_file_init(&f,7,true))==KOSDEC_ADX_MORE);size_t at=0;
    for(unsigned i=0;i<1000;++i) {
        if(f.stream.wraps==2)kosdec_adx_stream_release_loop(&f.stream);
        int16_t pcm[14];struct kosdec_adx_stream_result z=kosdec_adx_stream_read(&f.stream,pcm,7);
        for(size_t j=0;j<z.frames;++j){CHECK(at+j<123);if(at+j<123)CHECK(pcm[j*2]==vectors[4].pcm[0][at+j] && pcm[j*2+1]==vectors[4].pcm[1][at+j]);}
        at+=z.frames;if(z.status>=KOSDEC_ADX_DONE){CHECK(z.status==KOSDEC_ADX_DONE);break;}
    }
    CHECK(at==123 && f.stream.wraps==2 && !f.io_errno);
    }
    read_limit=7;
    unsigned before=calls;kosdec_adx_stream_cancel(&f.stream);CHECK(drain(&f)==KOSDEC_ADX_DONE && calls==before);
    for(int i=1;i<=6;++i) {
        fault=i;CHECK(kosdec_adx_file_init(&f,7,false)==KOSDEC_ADX_MORE);
        enum kosdec_adx_status expected=i==4||i==5?KOSDEC_ADX_DONE:i==6?KOSDEC_ADX_TRUNCATED:KOSDEC_ADX_IO_ERROR;
        CHECK(drain(&f)==expected);
        if(i==1)CHECK(f.io_errno==ESPIPE);if(i==2||i==3)CHECK(f.io_errno==EIO);
    }
    fault=0;CHECK(kosdec_adx_file_init(&f,8,false)==KOSDEC_ADX_IO_ERROR && f.io_errno==EBADF);
    length=(int64_t)UINT32_MAX+1;CHECK(kosdec_adx_file_init(&f,7,false)==KOSDEC_ADX_IO_ERROR && f.io_errno==EOVERFLOW);
    CHECK(kosdec_adx_file_init(NULL,7,false)==KOSDEC_ADX_BAD_ARGUMENT);
    CHECK(fseek(handle,0,SEEK_SET)==0 && fgetc(handle)==0x80); /* Still caller-owned. */
    buffered();
    fclose(handle);printf("ADX_FILE failures=%u seeks=%u reads=%u\n",failures,calls,reads);return failures?1:0;
}
