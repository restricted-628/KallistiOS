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
#include "snd_channel.h"

static uint64_t sfx_inuse;

uint64_t snd_channel_reserved(void) {
    return sfx_inuse;
}

int snd_channel_reserve(unsigned count, int channels[2]) {
    int selected[2];
    unsigned found = 0;
    uint64_t mask = 0;

    for(int chn = 0; chn < 64 && found < count; ++chn) {
        if(!(sfx_inuse & (UINT64_C(1) << chn))) {
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
    int result = snd_channel_reserve(1, channels);
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
