/* Original native player consumer. Supply /cd/test.adx; no SDK media included. */
#include <kos.h>
#include <dc/fs_iso9660.h>
#include <kosdec/adx_disc.h>
#include <kosdec/adx_player.h>
#include <errno.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);
#define PCM_CAPACITY 16384u
#define SOUND_BYTES 16384u
static alignas(32) uint8_t disc_buffer[32768], decode_stack[16384];
static alignas(32) int16_t pcm_storage[PCM_CAPACITY*2], scratch[SOUND_BYTES/2+1];
static struct kosdec_adx_disc disc;
static struct kosdec_adx_stream reader;
static struct kosdec_adx_player player;

static void retain(const char *why) {
    printf("ADX-PLAYER RETAIN: %s errno=%d; reset to exit\n",why,errno);
    for(;;)thd_sleep(1000);
}
static void production_pause(bool paused) {
    struct kosdec_adx_command command={.kind=KOSDEC_ADX_PRODUCTION_PAUSE,.enabled=paused};
    enum kosdec_adx_status status=kosdec_adx_player_post(&player,&command);
    if(status==KOSDEC_ADX_DONE)return; /* Tiny source can finish before control. */
    if(status!=KOSDEC_ADX_MORE)retain("control post");
    uint64_t deadline=timer_ms_gettime64()+2000;
    while(!kosdec_adx_player_take(&player,&status)) {
        if(timer_ms_gettime64()>=deadline)retain("control timeout");
        thd_sleep(1);
    }
    if(status!=KOSDEC_ADX_MORE && status!=KOSDEC_ADX_DONE)retain("control rejected");
}
int main(int argc,char **argv) {
    const char *path=argc>1?argv[1]:"/cd/test.adx";
    iso9660_file_info_t extent;
    if(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT)<0 ||
       fs_iso9660_get_path_info(path,&extent)<0)retain("direct ISO lookup");
    if(!extent.size || !extent.sector_count || extent.ext_attr_length ||
       extent.file_unit_size || extent.interleave_gap ||
       (extent.flags&(ISO9660_FILE_DIRECTORY|ISO9660_FILE_MULTI_EXTENT)))
        retain("unsupported ISO extent");
    cdrom_sector_range_t *range=cdrom_sector_range_open(extent.extent_fad,extent.sector_count);
    if(!range || kosdec_adx_disc_init(&disc,range,0,extent.size,disc_buffer,
       sizeof(disc_buffer),4000)!=KOSDEC_ADX_MORE)retain("disc source init");
    kosdec_adx_stream_init(&reader,kosdec_adx_disc_read_at,&disc,extent.size,false);
    /* App owns global audio initialization; a port should coordinate this with
       its existing sound subsystem instead of resetting it for each player. */
    if(snd_stream_init_ex(2,SOUND_BYTES)<0)retain("global stream init");
    struct kosdec_adx_player_config config={
        .pcm=pcm_storage,.pcm_samples=PCM_CAPACITY*2,.capacity=PCM_CAPACITY,
        .scratch=scratch,.scratch_samples=SOUND_BYTES/2+1,
        .stack=decode_stack,.stack_bytes=sizeof(decode_stack),.sound_bytes=SOUND_BYTES,
        .transfer_timeout_ms=1000,.prefill_timeout_ms=10000
    };
    if(kosdec_adx_player_init(&player,&reader,NULL,&config)<0 ||
       kosdec_adx_player_start(&player)<0)retain("player init/start");
    production_pause(true);production_pause(false);
    printf("ADX-PLAYER CONTROL pause/resume acknowledged or source already complete\n");
    uint64_t deadline=timer_ms_gettime64()+120000,total_frames=0;
    uint32_t seek_frame=0;
    bool started=false;
    struct kosdec_adx_player_info info;
    for(unsigned track=0;track<3;++track) {
        bool ended=false,cancelled=false;
        while(timer_ms_gettime64()<deadline) {
            if(kosdec_adx_player_poll(&player)<0)retain("player poll");
            if(kosdec_adx_player_get_info(&player,&info)<0)retain("player info");
            if(!started && info.sample_rate) {
                started=true;
                if(kosdec_adx_player_set_output(&player,255,255,info.channels==1?128:0,255)<0)
                    retain("player output control");
                printf("ADX-PLAYER START rate=%" PRIu32 " channels=%u\n",info.sample_rate,info.channels);
            }
            /* Test cancellation while the producer still owns queued PCM/I/O. */
            if(track==0 && info.sample_rate && info.source_frames>=info.sample_rate &&
               info.state==KOSDEC_ADX_PLAYER_PLAYING) {
                if(kosdec_adx_player_cancel_source(&player)<0)retain("source cancel/join");
                cancelled=ended=true;break;
            }
            if(info.state==KOSDEC_ADX_PLAYER_SOURCE_END){ended=true;break;}
            thd_sleep(2);
        }
        if(!ended)retain("diagnostic timeout");
        if(!cancelled && kosdec_adx_player_release_source(&player)<0)retain("source release/join");
        if(kosdec_adx_player_get_info(&player,&info)<0)retain("released info");
        if(info.source_frames>reader.returned_frames ||
           (!cancelled && (info.source_frames!=reader.returned_frames ||
            info.source_frames!=reader.decoder.total_frames-seek_frame)))retain("source PCM accounting mismatch");
        if(track==0 && (!cancelled || info.source_frames>=reader.decoder.total_frames))retain("missing early cancellation");
        total_frames+=info.source_frames;
        while(kosdec_adx_disc_close(&disc)!=KOSDEC_ADX_DONE) {
            if(kosdec_adx_player_poll(&player)<0)retain("idle output poll");
            thd_sleep(1);
        }
        /* Observation interval for the capture only, not a retirement guarantee. */
        uint64_t tail_deadline=timer_ms_gettime64()+1000+SOUND_BYTES*500/info.output_rate;
        while(timer_ms_gettime64()<tail_deadline) {
            if(kosdec_adx_player_poll(&player)<0)retain("tail poll");
            thd_sleep(2);
        }
        printf("ADX-PLAYER TRACK index=%u frames=%" PRIu64 " total=%" PRIu64
               " seek=%" PRIu32 " output_rate=%" PRIu32 " cancelled=%u\n",
               track+1,info.source_frames,total_frames,seek_frame,info.output_rate,cancelled?1:0);
        if(track!=2) {
            seek_frame=track==0?info.sample_rate+17:0;
            if(kosdec_adx_player_set_rate(&player,track==0?info.sample_rate/2:0)<0)retain("playback rate update");
            if(kosdec_adx_disc_init(&disc,range,0,extent.size,disc_buffer,
               sizeof(disc_buffer),4000)!=KOSDEC_ADX_MORE)retain("next disc source init");
            kosdec_adx_stream_init(&reader,kosdec_adx_disc_read_at,&disc,extent.size,false);
            if(kosdec_adx_stream_seek_begin(&reader,seek_frame)!=KOSDEC_ADX_MORE)retain("source seek begin");
            if(kosdec_adx_player_bind_source(&player,&reader,NULL)<0)retain("next source bind");
        }
    }
    if(kosdec_adx_player_stop(&player,2000)<0)retain("player software stop/join");
    if(kosdec_adx_player_get_info(&player,&info)<0)retain("stopped info");
    if(info.first_error || info.stop_error)retain("stored output failure");
    if(info.copied_frames!=total_frames || info.sources_started!=3)retain("PCM accounting mismatch");
    if(cdrom_sector_range_close(range)<0)retain("disc range close");
    printf("ADX-PLAYER RESULT frames=%" PRIu64 " requested=%" PRIu64
           " callbacks=%" PRIu64 " starvations=%" PRIu64 " copied_bytes=%" PRIu64
           " rate=%" PRIu32 " channels=%u retained=%d\n",
           info.copied_frames,info.requested_frames,info.callbacks,info.starvations,
           info.copied_frames*info.channels*2,info.sample_rate,info.channels,info.retained_sound);
    /* Sound handle/allocation remains retained. No destroy, restart or global
       shutdown until Audio provides a qualified sample-retirement contract. */
    for(;;)thd_sleep(1000);
}
