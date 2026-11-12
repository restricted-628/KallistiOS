/* Original direct-disc/fiber decode exerciser. No sound driver or media assets. */
#include <kos.h>
#include <kos/fiber.h>
#include <dc/fs_iso9660.h>
#include <kosdec/adx_disc.h>
#include <kosdec/adx_pipe.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static alignas(32) uint8_t input_buffer[32768];
static alignas(32) uint8_t decode_stack[16384];
static struct kosdec_adx_disc disc;
static struct kosdec_adx_sequence sequence;
static struct kosdec_adx_pipe pcm_pipe;
static int16_t pcm_ring[512];
static kfiber_t *main_fiber;
static uint64_t frames, produced, steps;
static uint32_t checksum;
static bool waiting;
static enum kosdec_adx_status result;

/* Sole owner of the decoder. All math finishes before a cooperative transfer;
   normal ABI FPU mode is retained and no XMTRX value is live across a yield. */
static void decode(void *unused) {
    (void)unused;
    for(;;) {
        struct kosdec_adx_stream_result r=kosdec_adx_pipe_pump(&pcm_pipe);
        ++steps;
        produced+=r.frames;
        result=r.status;
        if(r.status>=KOSDEC_ADX_DONE)return;
        waiting=r.status==KOSDEC_ADX_NEED_INPUT;
        /* A game can run other ready work on main between these slices. */
        if(fiber_switch(main_fiber)<0){result=KOSDEC_ADX_IO_ERROR;return;}
    }
}

static void retain(const char *why) {
    printf("ADX: %s; live storage retained, reset required\n",why);
    for(;;)thd_sleep(1000);
}

int main(int argc,char **argv) {
    const char *path=argc>1?argv[1]:"/cd/test.adx";
    iso9660_file_info_t info;
    /* This executable selects direct ISO before its own first lookup. A library
       must not change an application's already-mounted filesystem policy. */
    if(fs_iso9660_set_backend(FS_ISO9660_BACKEND_DIRECT)<0 ||
       fs_iso9660_get_path_info(path,&info)<0) {
        printf("ADX: cannot locate %s on direct ISO\n",path);return EXIT_FAILURE;
    }
    /* One ordinary contiguous extent only. Do not misinterpret interleaving,
       extended attributes, multi-extent records or directories as raw audio. */
    if(!info.size || !info.sector_count || info.ext_attr_length ||
       info.file_unit_size || info.interleave_gap ||
       (info.flags & (ISO9660_FILE_DIRECTORY | ISO9660_FILE_MULTI_EXTENT))) {
        printf("ADX: unsupported ISO extent\n");return EXIT_FAILURE;
    }
    cdrom_sector_range_t *range=cdrom_sector_range_open(info.extent_fad,info.sector_count);
    if(!range)return EXIT_FAILURE;
    if(kosdec_adx_disc_init(&disc,range,0,info.size,input_buffer,sizeof(input_buffer),4000)!=KOSDEC_ADX_MORE) {
        cdrom_sector_range_close(range);return EXIT_FAILURE;
    }
    kosdec_adx_sequence_init(&sequence,false);
    for(unsigned i=0;i<3;++i) {
        struct kosdec_adx_source item={kosdec_adx_disc_read_at,&disc,info.size,i};
        if(kosdec_adx_sequence_enqueue(&sequence,&item)!=KOSDEC_ADX_MORE) {
            kosdec_adx_disc_close(&disc);cdrom_sector_range_close(range);return EXIT_FAILURE;
        }
    }
    if(kosdec_adx_sequence_start(&sequence)!=KOSDEC_ADX_MORE) {
        kosdec_adx_disc_close(&disc);cdrom_sector_range_close(range);return EXIT_FAILURE;
    }
    if(kosdec_adx_pipe_init(&pcm_pipe,NULL,&sequence,pcm_ring,512,256)!=KOSDEC_ADX_MORE) {
        kosdec_adx_disc_close(&disc);cdrom_sector_range_close(range);return EXIT_FAILURE;
    }
    main_fiber=fiber_attach();
    kfiber_t *child=main_fiber?fiber_create(decode_stack,sizeof(decode_stack),decode,NULL):NULL;
    if(!child) {
        kosdec_adx_disc_close(&disc);cdrom_sector_range_close(range);return EXIT_FAILURE;
    }
    uint64_t start=timer_ms_gettime64(),last=start;
    bool drained=false;
    while(!drained) {
        if(fiber_get_state(child)==KFIBER_STATE_READY && fiber_switch(child)<0)
            retain("fiber switch failed");
        /* Main is the sole consumer. An eventual mixer can use the same pull
           boundary; this diagnostic consumes PCM without submitting audio. */
        int16_t pcm[94];
        struct kosdec_adx_pipe_result r=kosdec_adx_pipe_read(&pcm_pipe,pcm,47);
        uint32_t rate;unsigned channels=0;
        if(r.frames && !kosdec_adx_pipe_get_format(&pcm_pipe,&rate,&channels))
            retain("PCM without format");
        for(size_t i=0;i<r.frames*channels;++i)
            checksum=(checksum<<5 | checksum>>27) ^ (uint16_t)pcm[i];
        frames+=r.frames;
        drained=r.drained;
        if(!drained && r.producer==KOSDEC_ADX_MORE &&
           fiber_get_state(child)!=KFIBER_STATE_READY)
            retain("producer returned without closing PCM");
        uint64_t now=timer_ms_gettime64();
        if(now-last>=500) {
            printf("ADX: decoded=%" PRIu64 " frames, throughput=%" PRIu64 " frames/s\n",
                   produced,produced*1000/(now-start));
            last=now;
        }
        /* Only idle the carrier when this example has no runnable work. The
           request worker can then progress; do not busy-poll the drive. */
        if(waiting && fiber_get_state(child)==KFIBER_STATE_READY)thd_sleep(1);
    }
    if(fiber_get_state(child)!=KFIBER_STATE_FINISHED)retain("unexpected fiber state");
    /* Decoder return is not a transport-retirement certificate. */
    while(kosdec_adx_disc_close(&disc)!=KOSDEC_ADX_DONE)thd_sleep(1);
    if(fiber_destroy(child)<0 || cdrom_sector_range_close(range)<0)retain("release failed");
    uint64_t elapsed=timer_ms_gettime64()-start;
    printf("ADX: status=%u frames=%" PRIu64 " steps=%" PRIu64
           " ms=%" PRIu64 " checksum=%08" PRIx32 " entries=%" PRIu64 " io_errno=%d\n",
           (unsigned)result,frames,steps,elapsed,checksum,sequence.completed_entries,disc.io_errno);
    return result==KOSDEC_ADX_DONE && frames==produced?EXIT_SUCCESS:EXIT_FAILURE;
}
