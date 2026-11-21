/* Sole-owner register and refill fixture for private retained initial TL. */
#include <kos.h>
#include <dc/g2bus.h>
#include <dc/sound/stream.h>
#include <errno.h>
#include <string.h>
#include "snd_retained_start_tl.h"

enum { RING = 8192, SOURCE_FRAMES = 2 * RING };

typedef struct {
    unsigned channels;
    unsigned delivered;
} source_t;

static int fill(void *context, int16_t *samples, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    source_t *source = context;
    if(!capacity || frames != RING / 2) {
        errno = EINVAL;
        return -1;
    }
    memset(samples, 0, frames * source->channels * sizeof(*samples));
    if(source->delivered == SOURCE_FRAMES) {
        spans[0] = (snd_retained_pcm_span_t){0, 0, frames, SND_RETAINED_PCM_TERMINAL_SILENCE};
    }
    else {
        spans[0] = (snd_retained_pcm_span_t){1, source->delivered, frames, SND_RETAINED_PCM_SOURCE};
        source->delivered += frames;
    }
    return 1;
}

static unsigned read_tl(unsigned channel) {
    return (g2_read_32(0xa0700000u + channel * 0x80u + 0x28u) >> 8) & 0xffu;
}

static int active_slots(unsigned channels, unsigned slots[2]) {
    slots[0] = slots[1] = 64;
    for(unsigned ch = 0; ch < 64; ++ch) {
        snd_channel_status_ex_t status;
        if(snd_channel_get_status_ex(ch, &status) < 0)
            return -1;
        if(!status.configured || !status.playing)
            continue;
        unsigned side = channels == 2 && status.config.pan == 255;
        if(slots[side] != 64 || (channels == 2 &&
           status.config.pan != 0 && status.config.pan != 255)) {
            errno = EPROTO;
            return -1;
        }
        slots[side] = ch;
    }
    for(unsigned i = 0; i < channels; ++i) {
        if(slots[i] == 64) {
            errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

#define CHECK(test) do { \
    if(!(test)) { \
        printf("RETAINED START TL FAIL channels=%u line=%d errno=%d: %s\n", \
               channels, __LINE__, errno, #test); \
        goto fail; \
    } \
} while(0)

static int run(unsigned channels, unsigned left_tl, unsigned right_tl) {
    static source_t source;
    source = (source_t){channels, 0};
    snd_retained_pcm_t *pcm = NULL;
    snd_retained_pcm_snapshot_t cursor;
    unsigned slots[2];
    CHECK(snd_retained_pcm_open(&pcm, channels, 44100, RING, fill, &source) == 0);
    CHECK(snd_retained_pcm_start_with_tl(pcm, left_tl, right_tl) == 0);
    CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && cursor.valid);
    CHECK(active_slots(channels, slots) == 0);
    CHECK(read_tl(slots[0]) == left_tl);
    if(channels == 2)
        CHECK(read_tl(slots[1]) == right_tl);

    uint64_t initial_output = cursor.output_frames;
    uint64_t end = timer_ms_gettime64() + 2000;
    bool sealed = false;
    do {
        uint64_t previous = cursor.output_frames;
        CHECK(snd_retained_pcm_poll(pcm) == 0);
        if(!sealed && source.delivered == SOURCE_FRAMES) {
            CHECK(snd_retained_pcm_seal(pcm, 1, SOURCE_FRAMES) == 0);
            sealed = true;
        }
        CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 &&
              cursor.valid && cursor.output_frames >= previous);
        if(cursor.source_output_end)
            break;
        thd_sleep(1);
    } while(timer_ms_gettime64() < end);

    CHECK(cursor.source_output_end && cursor.source_frames == SOURCE_FRAMES &&
          cursor.output_frames > initial_output && cursor.output_frames > RING);
    CHECK(read_tl(slots[0]) == left_tl);
    if(channels == 2)
        CHECK(read_tl(slots[1]) == right_tl);
    CHECK(snd_retained_pcm_stop(pcm, 250) == 0);
    CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && !cursor.valid && cursor.software_stopped);
    CHECK(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY);
    printf("RETAINED START TL channels=%u TL=%u/%u source=%u\n",
           channels, left_tl, right_tl, (unsigned)cursor.source_frames);
    return 0;

fail:
    if(pcm)
        snd_retained_pcm_stop(pcm, 250);
    return -1;
}

int main(void) {
    if(snd_stream_init_ex(2, 2 * RING) < 0)
        printf("RETAINED START TL FAIL init errno=%d\n", errno);
    else if(run(1, 60, 255) == 0 && run(2, 60, 0) == 0)
        puts("RETAINED START TL PASS: mono and stereo readback, source credit after wrap, retained stop");
    for(;;)
        thd_sleep(1000);
}
