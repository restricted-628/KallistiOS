/* Single-lease mono fixture for live raw TL and retained source attribution. */
#include <kos.h>
#include <dc/g2bus.h>
#include <dc/sound/stream.h>
#include <errno.h>
#include <inttypes.h>
#include "snd_retained_gain.h"

enum { RING = 8192, SOURCE_FRAMES = 3 * RING + 137 };
static unsigned delivered;

#define CHECK(test) do { \
    if(!(test)) { \
        printf("RETAINED MONO GAIN FAIL line=%d errno=%d: %s\n", __LINE__, errno, #test); \
        goto done; \
    } \
} while(0)

static int fill(void *context, int16_t *pcm, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    (void)context;
    if(capacity < 2 || frames != RING / 2) {
        errno = EINVAL;
        return -1;
    }
    unsigned count = SOURCE_FRAMES - delivered;
    if(count > frames)
        count = frames;
    unsigned used = 0;
    if(count) {
        for(unsigned i = 0; i < count; ++i)
            pcm[i] = (int16_t)(100 + (delivered + i) % 15000);
        spans[used++] = (snd_retained_pcm_span_t){1, delivered, count, SND_RETAINED_PCM_SOURCE};
        delivered += count;
    }
    if(count < frames)
        spans[used++] = (snd_retained_pcm_span_t){0, 0, frames - count, SND_RETAINED_PCM_TERMINAL_SILENCE};
    return (int)used;
}

static int active_channel(void) {
    int channel = -1;
    for(unsigned ch = 0; ch < 64; ++ch) {
        snd_channel_status_ex_t status;
        if(snd_channel_get_status_ex(ch, &status) < 0)
            return -1;
        if(status.configured && status.playing) {
            if(channel >= 0) {
                errno = EPROTO;
                return -1;
            }
            channel = (int)ch;
        }
    }
    if(channel < 0)
        errno = ENODEV;
    return channel;
}

static unsigned read_tl(unsigned channel) {
    return (g2_read_32(0xa0700000u + channel * 0x80u + 0x28u) >> 8) & 0xffu;
}

int main(void) {
    const unsigned targets[] = {0, RING, 2 * RING};
    const unsigned values[] = {60, 255, 0};
    uint64_t observations[3] = {0};
    snd_retained_pcm_t *pcm = NULL;
    snd_retained_pcm_snapshot_t cursor;
    uint64_t request = 0;
    unsigned gains = 0;
    bool pending = false, sealed = false;

    CHECK(snd_stream_init_ex(1, 2 * RING) == 0);
    CHECK(snd_retained_pcm_open(&pcm, 1, 44100, RING, fill, NULL) == 0);
    CHECK(snd_retained_pcm_start(pcm) == 0);
    CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && cursor.valid);
    const uint64_t epoch = cursor.epoch;
    const uint32_t session = cursor.session;
    int channel = active_channel();
    CHECK(channel >= 0 && read_tl((unsigned)channel) == 0);

    uint64_t deadline = timer_ms_gettime64() + 2000;
    do {
        if(!pending && gains < 3 && cursor.output_frames >= targets[gains]) {
            CHECK(snd_retained_pcm_gain_begin(pcm, values[gains], 123, &request) == 0 &&
                  request == gains + 1);
            pending = true;
        }
        uint64_t previous = cursor.output_frames;
        CHECK(snd_retained_pcm_poll(pcm) == 0);
        if(!sealed && delivered == SOURCE_FRAMES) {
            CHECK(snd_retained_pcm_seal(pcm, 1, SOURCE_FRAMES) == 0);
            sealed = true;
        }
        CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 &&
              cursor.epoch == epoch && cursor.session == session &&
              cursor.output_frames >= previous);
        if(pending) {
            snd_retained_gain_status_t gain;
            CHECK(snd_retained_pcm_gain_status(pcm, request, &gain) == 0 &&
                  gain.epoch == epoch && gain.session == session &&
                  gain.requested_tl[0] == values[gains] &&
                  !gain.requested_tl[1] && !gain.acknowledged_tl[1] &&
                  !((gain.admitted | gain.applied | gain.rejected |
                     gain.unknown | gain.acknowledged) & ~SND_RETAINED_GAIN_LEFT));
            if(gain.state == SND_RETAINED_GAIN_APPLIED) {
                CHECK(gain.applied == 1 && gain.admitted == 1 && gain.acknowledged == 1 &&
                      !gain.error && !gain.rejected && !gain.unknown && cursor.valid &&
                      gain.acknowledged_tl[0] == values[gains] && read_tl((unsigned)channel) == values[gains]);
                observations[gains++] = cursor.output_frames;
                pending = false;
            }
            else
                CHECK(gain.state == SND_RETAINED_GAIN_PENDING && !cursor.valid);
        }
        if(cursor.valid) {
            uint64_t expected = cursor.output_frames < SOURCE_FRAMES ? cursor.output_frames : SOURCE_FRAMES;
            CHECK(cursor.source_frames == expected && cursor.next_source_frame == expected);
        }
        else
            CHECK(pending);
        if(cursor.source_output_end)
            break;
        thd_sleep(1);
    } while(timer_ms_gettime64() < deadline);

    CHECK(sealed && !pending && gains == 3 && cursor.source_output_end &&
          cursor.source_frames == SOURCE_FRAMES && cursor.output_frames > 3 * RING);
    CHECK(observations[0] < observations[1] && observations[1] < observations[2]);
    CHECK(snd_retained_pcm_stop(pcm, 250) == 0);
    CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && cursor.software_stopped && !cursor.valid);
    CHECK(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY);
    printf("RETAINED MONO GAIN TL60@%" PRIu64 " TL255@%" PRIu64
           " TL0@%" PRIu64 " source=%" PRIu64 " output=%" PRIu64 "\n",
           observations[0], observations[1], observations[2], cursor.source_frames, cursor.output_frames);
    puts("RETAINED MONO GAIN PASS: three raw TL changes, exact source through three wraps, retained stop");
done:
    if(pcm)
        snd_retained_pcm_stop(pcm, 250);
    for(;;)
        thd_sleep(1000);
}
