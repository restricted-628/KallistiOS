/* A sole-owner guest fixture for live raw TL on retained PCM stereo. */
#include <kos.h>
#include <dc/g2bus.h>
#include <dc/sound/stream.h>
#include <dc/sound/aica_observation.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include "snd_retained_gain.h"

#define RING 8192u
#define SOURCE_FRAMES (RING * 2)
#define CHECK(test) do { \
    if(!(test)) { \
        printf("RETAINED GAIN FAIL line=%d errno=%d: %s\n", __LINE__, errno, #test); \
        goto done; \
    } \
} while(0)

static uint32_t delivered;

static int fill(void *context, int16_t *pcm, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    (void)context;
    if(!capacity || frames != RING / 2) {
        errno = EINVAL;
        return -1;
    }
    memset(pcm, 0, frames * 4);
    if(delivered == SOURCE_FRAMES) {
        spans[0] = (snd_retained_pcm_span_t){0, 0, frames, SND_RETAINED_PCM_TERMINAL_SILENCE};
        return 1;
    }
    spans[0] = (snd_retained_pcm_span_t){1, delivered, frames, SND_RETAINED_PCM_SOURCE};
    delivered += frames;
    return 1;
}

static int find_pair(unsigned pair[2]) {
    pair[0] = pair[1] = 64;
    for(unsigned ch = 0; ch < 64; ++ch) {
        snd_channel_status_ex_t status;
        if(snd_channel_get_status_ex(ch, &status) < 0)
            return -1;
        if(!status.configured || !status.playing)
            continue;
        unsigned side = status.config.pan == 255;
        if((status.config.pan != 0 && status.config.pan != 255) || pair[side] != 64)
            return -1;
        pair[side] = ch;
    }
    return pair[0] < 64 && pair[1] < 64 ? 0 : -1;
}

static unsigned snapshot_registers(unsigned ch, uint32_t stable[6]) {
    uintptr_t base = 0xa0700000u + ch * 0x80u;
    stable[0] = g2_read_32(base) & 0x3fffu; /* address, format and loop */
    stable[1] = g2_read_32(base + 0x04);
    stable[2] = g2_read_32(base + 0x08);
    stable[3] = g2_read_32(base + 0x0c);
    stable[4] = g2_read_32(base + 0x18); /* pitch */
    uint32_t envelope = g2_read_32(base + 0x28);
    stable[5] = envelope & 0xffff00ffu;
    return (envelope >> 8) & 0xffu;
}

int main(void) {
    snd_retained_pcm_t *pcm = NULL;
    snd_driver_status_t driver;
    snd_retained_pcm_snapshot_t before, cursor;
    snd_retained_gain_status_t gain;
    unsigned pair[2];
    uint32_t baseline[2][6];
    uint64_t id = 0;

    CHECK(snd_stream_init_ex(2, RING * 2) == 0);
    CHECK(snd_driver_get_status(&driver, 100) == 0 &&
          (driver.features & AICA_DRIVER_FEATURE_OBSERVATION_TL));
    CHECK(snd_retained_pcm_open(&pcm, 2, 44100, RING, fill, NULL) == 0);
    CHECK(snd_retained_pcm_start(pcm) == 0);
    CHECK(find_pair(pair) == 0);
    for(unsigned side = 0; side < 2; ++side)
        CHECK(snapshot_registers(pair[side], baseline[side]) == 0);

    const unsigned vectors[][2] = {{127, 128}, {254, 255}};
    CHECK(snd_retained_pcm_gain_begin(pcm, 127, 256, &id) == -1 && errno == EINVAL && !id);
    for(unsigned i = 0; i < 2; ++i) {
        CHECK(snd_retained_pcm_poll(pcm) == 0);
        CHECK(snd_retained_pcm_query(pcm, 1, &before) == 0 && before.valid);
        CHECK(snd_retained_pcm_gain_begin(pcm, vectors[i][0], vectors[i][1], &id) == 0 &&
              id == i + 1);
        CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 &&
              !cursor.valid && cursor.output_frames == before.output_frames);
        uint64_t until = timer_ms_gettime64() + 150;
        do {
            CHECK(snd_retained_pcm_poll(pcm) == 0);
            CHECK(snd_retained_pcm_gain_status(pcm, id, &gain) == 0);
            if(gain.state != SND_RETAINED_GAIN_PENDING)
                break;
            thd_sleep(1);
        } while(timer_ms_gettime64() < until);
        CHECK(gain.state == SND_RETAINED_GAIN_APPLIED && gain.admitted == 3 &&
              gain.applied == 3 && gain.acknowledged == 3 &&
              !gain.unknown && !gain.rejected && !gain.error);
        CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && cursor.valid &&
              cursor.output_frames > before.output_frames && cursor.output_frames == cursor.source_frames &&
              cursor.epoch == before.epoch && cursor.epoch == gain.epoch &&
              cursor.session == before.session && cursor.session == gain.session);
        for(unsigned side = 0; side < 2; ++side) {
            uint32_t current[6];
            CHECK(snapshot_registers(pair[side], current) == vectors[i][side] &&
                  gain.acknowledged_tl[side] == vectors[i][side] &&
                  !memcmp(current, baseline[side], sizeof(current)));
        }
        printf("RETAINED GAIN TL %u/%u cursor %" PRIu64 " -> %" PRIu64 "\n",
               vectors[i][0], vectors[i][1], before.output_frames, cursor.output_frames);
    }

    bool sealed = false;
    uint64_t until = timer_ms_gettime64() + 1500;
    do {
        uint64_t previous = cursor.output_frames;
        CHECK(snd_retained_pcm_poll(pcm) == 0);
        if(!sealed && delivered == SOURCE_FRAMES) {
            CHECK(snd_retained_pcm_seal(pcm, 1, delivered) == 0);
            sealed = true;
        }
        CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 &&
              cursor.valid && cursor.output_frames >= previous);
        if(cursor.source_output_end)
            break;
        thd_sleep(1);
    } while(timer_ms_gettime64() < until);
    CHECK(cursor.source_output_end && cursor.source_frames == SOURCE_FRAMES);
    CHECK(snd_retained_pcm_gain_begin(pcm, 0, 0, &id) == 0);
    CHECK(snd_retained_pcm_stop(pcm, 250) == 0);
    CHECK(snd_retained_pcm_gain_status(pcm, id, &gain) == 0 &&
          gain.state == SND_RETAINED_GAIN_FAILED && gain.error == ECANCELED && !gain.admitted);
    CHECK(snd_retained_pcm_query(pcm, 1, &cursor) == 0 && cursor.software_stopped && !cursor.valid);
    CHECK(snd_retained_pcm_close(pcm) == -1 && errno == EBUSY);
    puts("RETAINED GAIN PASS: raw stereo TL, unchanged pitch and geometry, continuous source clock, checked retained stop");

done:
    if(pcm)
        snd_retained_pcm_stop(pcm, 250);
    for(;;)
        thd_sleep(1000);
}
