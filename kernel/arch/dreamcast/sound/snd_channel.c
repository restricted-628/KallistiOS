/* KallistiOS ##version##
   Copyright (C) 2000, 2001, 2002, 2003, 2004 Megan Potter
   Copyright (C) 2023, 2024 Ruslan Rostovtsev
   Copyright (C) 2023 Andy Barajas
   Copyright (C) 2024 Stefanos Kornilios Mitsis Poiitidis
   Copyright (C) 2026 Joseph Black
   Shared cooperative channel reservations, factored from snd_sfxmgr.c. */
#include <errno.h>
#include <kos/irq.h>
#include <dc/sound/sfxmgr.h>
#include <dc/sound/port.h>
#include "snd_channel.h"

static uint64_t sfx_inuse;
static uint64_t allowed[SND_CHANNEL_CLASS_COUNT] = {
    [SND_CHANNEL_CLASS_MIDI] = UINT64_MAX,
    [SND_CHANNEL_CLASS_SHOT] = UINT64_MAX,
    [SND_CHANNEL_CLASS_PCM] = UINT64_MAX
};

static uint64_t channel_range(unsigned first, unsigned count) {
    if(!count)
        return 0;
    if(count == 64)
        return UINT64_MAX;
    return ((UINT64_C(1) << count) - 1) << first;
}

int snd_voice_set_limits(const snd_voice_limits_t *limits) {
    uint64_t midi = UINT64_MAX, shot = UINT64_MAX, pcm = UINT64_MAX;

    if(limits) {
        snd_voice_limits_t value = *limits;
        if(value.midi > 64 || value.shot > 64 || value.pcm > 16 ||
           value.midi + value.shot + value.pcm > 64) {
            errno = EINVAL;
            return -1;
        }
        midi = channel_range(0, value.midi);
        shot = channel_range(value.midi, value.shot);
        pcm = channel_range(value.midi + value.shot, value.pcm);
    }

    int old = irq_disable();
    if(sfx_inuse) {
        irq_restore(old);
        errno = EBUSY;
        return -1;
    }
    allowed[SND_CHANNEL_CLASS_MIDI] = midi;
    allowed[SND_CHANNEL_CLASS_SHOT] = shot;
    allowed[SND_CHANNEL_CLASS_PCM] = pcm;
    irq_restore(old);
    return 0;
}

uint64_t snd_channel_reserved(void) {
    return sfx_inuse;
}

uint64_t snd_channel_shot_unavailable(void) {
    return snd_channel_reserved() | ~allowed[SND_CHANNEL_CLASS_SHOT];
}

int snd_channel_reserve(snd_channel_class_t source, unsigned count, int channels[2]) {
    uint64_t available = allowed[source] & ~sfx_inuse;
    int selected[2];
    unsigned found = 0;
    uint64_t mask = 0;

    for(int chn = 0; chn < 64 && found < count; ++chn) {
        if(available & (UINT64_C(1) << chn)) {
            selected[found++] = chn;
            mask |= UINT64_C(1) << chn;
        }
    }
    if(found != count) {
        errno = ENOSPC;
        return -1;
    }
    sfx_inuse |= mask;
    for(unsigned i = 0; i < count; ++i)
        channels[i] = selected[i];
    return 0;
}

void snd_channel_release(unsigned count, const int channels[2]) {
    for(unsigned i = 0; i < count; ++i)
        sfx_inuse &= ~(UINT64_C(1) << channels[i]);
}

int snd_sfx_chn_alloc(void) {
    int channels[2], old = irq_disable();
    int result = snd_channel_reserve(SND_CHANNEL_CLASS_SHOT, 1, channels);
    irq_restore(old);
    return result < 0 ? -1 : channels[0];
}

void snd_sfx_chn_free(int chn) {
    int old, channels[2] = { chn, -1 };
    if(chn < 0 || chn >= 64) {
        errno = EINVAL;
        return;
    }
    old = irq_disable();
    snd_channel_release(1, channels);
    irq_restore(old);
}
