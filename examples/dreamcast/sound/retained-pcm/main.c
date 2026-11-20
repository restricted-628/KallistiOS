/* Original silence/PCM attribution fixture for the private retained stream. */
#include <kos.h>
#include <dc/sound/stream.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include "snd_retained_pcm.h"

#define RING 8192u

static unsigned fills;

static int fill(void *context, int16_t *data, uint32_t frames,
                snd_retained_pcm_span_t *spans, unsigned capacity) {
    (void)context;
    if(frames != RING / 2 || capacity < 4) {
        errno = EINVAL;
        return -1;
    }
    /* Nonzero sentinel in the three kinds of inserted silence. The backend
       owns replacing them with zeros. The first source is zero-valued PCM. */
    memset(data, 0x55, frames * 4);
    if(!fills++) {
        memset(data, 0, 100 * 4);
        spans[0] = (snd_retained_pcm_span_t){1, 5, 100, SND_RETAINED_PCM_SOURCE};
        spans[1] = (snd_retained_pcm_span_t){0, 0, 11, SND_RETAINED_PCM_STARVATION};
        spans[2] = (snd_retained_pcm_span_t){1, 105, 19, SND_RETAINED_PCM_SOURCE};
        spans[3] = (snd_retained_pcm_span_t){2, 0, frames - 130, SND_RETAINED_PCM_SOURCE};
        return 4;
    }
    if(fills == 2) {
        spans[0] = (snd_retained_pcm_span_t){2, frames - 130, 27, SND_RETAINED_PCM_SOURCE};
        spans[1] = (snd_retained_pcm_span_t){0, 0, frames - 27, SND_RETAINED_PCM_TERMINAL_SILENCE};
        return 2;
    }
    spans[0] = (snd_retained_pcm_span_t){0, 0, frames, SND_RETAINED_PCM_TERMINAL_SILENCE};
    return 1;
}

int main(void) {
    snd_retained_pcm_t *pcm;
    snd_retained_pcm_snapshot_t a, b;
    if(snd_stream_init_ex(2, RING * 2) < 0 ||
       snd_retained_pcm_open(&pcm, 2, 44100, RING, fill, NULL) < 0) {
        printf("RETAINED PCM FAIL open errno=%d\n", errno);
        goto stay;
    }
    if(snd_retained_pcm_start(pcm) < 0 ||
       snd_retained_pcm_seal(pcm, 1, 119) < 0 ||
       snd_retained_pcm_seal(pcm, 2, RING / 2 - 103) < 0) {
        printf("RETAINED PCM FAIL start/seal errno=%d\n", errno);
        goto stop;
    }
    uint64_t until = timer_ms_gettime64() + 1500;
    do {
        if(snd_retained_pcm_poll(pcm) < 0 ||
           snd_retained_pcm_query(pcm, 1, &a) < 0 ||
           snd_retained_pcm_query(pcm, 2, &b) < 0 || !a.valid || !b.valid) {
            printf("RETAINED PCM FAIL poll/query errno=%d fills=%u\n", errno, fills);
            goto stop;
        }
        if(b.output_frames > RING + 256 && a.source_output_end && b.source_output_end)
            break;
        thd_sleep(1);
    } while(timer_ms_gettime64() < until);
    if(b.output_frames <= RING + 256 || a.source_frames != 119 ||
       a.next_source_frame != 124 || b.source_frames != RING / 2 - 103 ||
       b.next_source_frame != RING / 2 - 103 || !a.source_output_end ||
       !b.source_output_end || fills < 4) {
        printf("RETAINED PCM FAIL ledger output=%" PRIu64 " fills=%u\n",
               b.output_frames, fills);
        goto stop;
    }
    if(snd_retained_pcm_stop(pcm, 250) < 0 ||
       snd_retained_pcm_query(pcm, 2, &b) < 0 || !b.software_stopped ||
       b.valid || snd_retained_pcm_close(pcm) != -1 || errno != EBUSY) {
        printf("RETAINED PCM FAIL retained stop errno=%d\n", errno);
        goto stay;
    }
    puts("RETAINED PCM PASS: stereo wrap, two sealed sources, silence excluded, played lease retained");
    goto stay;
stop:
    if(snd_retained_pcm_stop(pcm, 250) < 0)
        printf("RETAINED PCM FAIL cleanup errno=%d\n", errno);
stay:
    for(;;)
        thd_sleep(1000);
}
