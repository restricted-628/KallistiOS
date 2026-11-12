/* Real file->decoder->service->player composition; sound/fibers are host models. */
#include <kosdec/adx_file.h>
#include <kosdec/adx_memory.h>
#include <kosdec/adx_player.h>
#include "player_mock.h"
#include <errno.h>
#include <pthread.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_fixture.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FILE_PLAYER_FAIL line=%u\n",(unsigned)__LINE__); abort(); } } while(0)
static pthread_t owner;
static FILE *handle;
static size_t extent, fragment;
static unsigned reads;
static bool retry, fail;
static uint8_t *asset;
static struct kosdec_adx_file file;
static struct kosdec_adx_player player;
static struct kosdec_adx_sequence sequence;
static alignas(32) uint8_t cache[4096], stack[16384];
static alignas(32) int16_t ring[2048], scratch[257];
enum { TOTAL = 4097, LOOP_START = 32, LOOP_END = 3009 };

int64_t fs_total64(file_t fd) { CHECK(fd==7); return (int64_t)extent; }
_off64_t fs_seek64(file_t fd, _off64_t off, int whence) {
    CHECK(fd==7 && whence==SEEK_SET && !pthread_equal(pthread_self(),owner));
    return fseeko(handle,(off_t)off,whence) ? -1 : ftello(handle);
}
ssize_t fs_read(file_t fd, void *dst, size_t bytes) {
    CHECK(fd==7 && bytes<=sizeof(cache) && !pthread_equal(pthread_self(),owner));
    ++reads;
    if(fail) { errno=EIO; return -1; }
    if(retry && reads%11==0) { errno=EAGAIN; return -1; }
    if(bytes>fragment) bytes=fragment;
    return (ssize_t)fread(dst,1,bytes,handle);
}
static void init(unsigned ch, unsigned rate, unsigned mode, size_t short_read, bool error) {
    mock_reset(); reads=0; retry=short_read<sizeof(cache); fail=error; fragment=short_read;
    size_t small_bytes; uint8_t *small=stream(50,18,ch,32,65,&small_bytes);
    size_t groups=(TOTAL+31)/32;
    extent=50+groups*18*ch+18; asset=calloc(extent,1); CHECK(asset);
    memcpy(asset,small,50); put32(asset+8,rate); put32(asset+12,TOTAL);
    if(mode==1) {
        put32(asset+28,LOOP_START); put32(asset+32,50+LOOP_START/32*18*ch);
        put32(asset+36,LOOP_END); put32(asset+40,50+(LOOP_END+31)/32*18*ch);
    }
    else memset(asset+20,0,24);
    for(size_t g=0;g<groups;++g) memcpy(asset+50+g*18*ch,small+50+(g%3)*18*ch,18*ch);
    memcpy(asset+extent-18,small+small_bytes-18,18); free(small);
    handle=tmpfile(); CHECK(handle && fwrite(asset,1,extent,handle)==extent); fflush(handle);
    CHECK(kosdec_adx_file_init_buffered(&file,7,mode==1,cache,sizeof(cache))==KOSDEC_ADX_MORE);
    kosdec_adx_sequence_init(&sequence,mode==2);
    struct kosdec_adx_source source={file.stream.read_at,file.stream.context,file.stream.input_bytes,1};
    CHECK(kosdec_adx_sequence_enqueue(&sequence,&source)==KOSDEC_ADX_MORE);
    CHECK(kosdec_adx_sequence_start(&sequence)==KOSDEC_ADX_MORE);
    struct kosdec_adx_player_config config={
        .pcm=ring,.pcm_samples=2048,.capacity=1024,.scratch=scratch,.scratch_samples=257,
        .stack=stack,.stack_bytes=sizeof(stack),.sound_bytes=512,
        .transfer_timeout_ms=1000,.prefill_timeout_ms=10000
    };
    CHECK(!kosdec_adx_player_init(&player,mode==2?NULL:&file.stream,mode==2?&sequence:NULL,&config));
    CHECK(!kosdec_adx_player_start(&player));
}
static struct kosdec_adx_player_info info(void) {
    struct kosdec_adx_player_info i; CHECK(!kosdec_adx_player_get_info(&player,&i)); return i;
}
static void finish(unsigned mode) {
    bool posted=false,received=false;
    for(unsigned step=0;step<30000;++step) {
        CHECK(!kosdec_adx_player_poll(&player));
        struct kosdec_adx_player_info i=info();
        if(mode && !posted && i.source_frames>=TOTAL*2) {
            struct kosdec_adx_command command={
                .kind=mode==1?KOSDEC_ADX_RELEASE_LOOP:KOSDEC_ADX_SEQUENCE_REPEAT,.enabled=false
            };
            CHECK(kosdec_adx_player_post(&player,&command)==KOSDEC_ADX_MORE); posted=true;
        }
        enum kosdec_adx_status receipt;
        if(posted && !received && kosdec_adx_player_take(&player,&receipt)) {
            CHECK(receipt==KOSDEC_ADX_MORE); received=true;
        }
        if(i.state==KOSDEC_ADX_PLAYER_SOURCE_END) {
            CHECK(!mode || (posted && received));
            CHECK(!kosdec_adx_player_release_source(&player)); return;
        }
        mock_pause();
    }
    CHECK(false);
}
static void compare(unsigned ch, unsigned mode) {
    size_t frames=mock_frames, at=0;
    uint64_t wraps=0;
    if(mode==1) {
        CHECK(frames>=TOTAL && (frames-TOTAL)%(LOOP_END-LOOP_START)==0);
        wraps=(frames-TOTAL)/(LOOP_END-LOOP_START); CHECK(wraps>0);
    }
    else if(mode==2) CHECK(frames>=TOTAL*2 && frames%TOTAL==0);
    else CHECK(frames==TOTAL);
    struct kosdec_adx_memory memory;
    kosdec_adx_memory_init(&memory,asset,extent,mode==1);
    for(unsigned step=0;step<30000;++step) {
        if(mode==1 && memory.wraps==wraps) kosdec_adx_memory_release_loop(&memory);
        int16_t expected[64];
        struct kosdec_adx_memory_result r=kosdec_adx_memory_read(&memory,expected,32);
        CHECK(at+r.frames<=frames);
        CHECK(!memcmp(mock_pcm+at*ch,expected,r.frames*ch*sizeof(*expected))); at+=r.frames;
        if(r.status>=KOSDEC_ADX_DONE) {
            CHECK(r.status==KOSDEC_ADX_DONE);
            if(at==frames) return;
            CHECK(mode==2); kosdec_adx_memory_init(&memory,asset,extent,false);
        }
    }
    CHECK(false);
}
static void cleanup(void) {
    CHECK(!kosdec_adx_player_stop(&player,1000));
    CHECK(!mock_callback && !mock_userdata);
    unsigned before=reads; fclose(handle); handle=NULL; free(asset);
    CHECK(!kosdec_adx_player_stop(&player,1000) && reads==before);
}
int main(void) {
    owner=pthread_self(); unsigned cases=0;
    for(unsigned ch=1;ch<=2;++ch) for(unsigned mode=0;mode<3;++mode)
    for(unsigned short_read=0;short_read<2;++short_read) {
        init(ch,short_read?22050:44100,mode,short_read?97:4096,false);
        finish(mode); compare(ch,mode);
        CHECK(!info().first_error && mock_allocs==1 && mock_starts==1 && mock_updates==0);
        /* A released file can be sought/rebound without reopening sound. */
        if(!mode) {
            size_t before=mock_frames;
            CHECK(kosdec_adx_stream_seek_begin(&file.stream,33)==KOSDEC_ADX_MORE);
            CHECK(!kosdec_adx_player_bind_source(&player,&file.stream,NULL)); finish(0);
            CHECK(mock_frames-before==TOTAL-33);
            CHECK(!memcmp(mock_pcm+before*ch,mock_pcm+33*ch,(TOTAL-33)*ch*sizeof(int16_t)));
            CHECK(mock_allocs==1 && mock_starts==1 && mock_updates==0 && info().sources_started==2);
        }
        cleanup(); ++cases;
    }
    /* Input failure retires the worker before its file can close. */
    init(2,44100,0,4096,true);
    bool failed=false;
    for(unsigned n=0;n<10000;++n) {
        if(kosdec_adx_player_poll(&player)<0) { failed=true; break; }
        mock_pause();
    }
    CHECK(failed && info().first_error==EIO && file.io_errno==EIO); cleanup(); ++cases;
    /* Cancel during steady looping, then prove no VFS access after join. */
    init(2,44100,1,97,false);
    for(unsigned n=0;info().source_frames<1024 && n<10000;++n) {
        CHECK(!kosdec_adx_player_poll(&player)); mock_pause();
    }
    CHECK(info().source_frames>=1024);
    CHECK(!kosdec_adx_player_cancel_source(&player));
    unsigned before=reads;
    for(unsigned n=0;n<4;++n) CHECK(!kosdec_adx_player_poll(&player));
    CHECK(reads==before && info().state==KOSDEC_ADX_PLAYER_IDLE); cleanup(); ++cases;
    printf("ADX_FILE_PLAYER cases=%u failures=0\n",cases);
    return 0;
}
