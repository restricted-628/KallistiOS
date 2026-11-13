/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Software-only reservations: no sound initialization or playback. */
#include <kos.h>
#include <dc/sound/port.h>
#include <dc/sound/sfxmgr.h>
#include <errno.h>
#include <stdio.h>

KOS_INIT_FLAGS(INIT_DEFAULT);
#define CHECK(x) do { if(!(x)) { printf("port-reservation: FAIL line %d errno %d\n", __LINE__, errno); return 1; } } while(0)
int main(void) {
    snd_port_t pair, stale, mono, all[32];
    snd_port_info_t info;
    int legacy[63];
    for(int i = 0; i < 63; ++i) {
        legacy[i] = snd_sfx_chn_alloc();
        CHECK(legacy[i] == i);
    }
    CHECK(snd_port_reserve(2, &pair) == -1 && errno == ENOSPC);
    CHECK(snd_port_reserve(1, &mono) == 0);
    CHECK(snd_port_query(mono, &info) == 0 && info.channels[0] == 63);
    CHECK(snd_sfx_chn_alloc() == -1 && errno == ENOSPC);
    CHECK(snd_port_close(mono) == 0);
    stale = mono;
    snd_sfx_chn_free(17);
    CHECK(snd_port_reserve(2, &pair) == 0);
    CHECK(snd_port_query(pair, &info) == 0 && info.channel_count == 2 && info.channels[0] == 17 && info.channels[1] == 63);
    CHECK(snd_port_close(stale) == -1 && errno == EINVAL);
    CHECK(snd_port_close(pair) == 0);
    for(int i = 0; i < 63; ++i)
        if(i != 17) snd_sfx_chn_free(legacy[i]);
    for(int i = 0; i < 32; ++i)
        CHECK(snd_port_reserve(2, &all[i]) == 0);
    CHECK(snd_port_reserve(1, &mono) == -1 && errno == ENOSPC);
    for(int i = 0; i < 32; ++i)
        CHECK(snd_port_close(all[i]) == 0);
    CHECK(snd_sfx_chn_alloc() == 0);
    snd_sfx_chn_free(0);
    puts("port-reservation: PASS (software ownership only)");
    fflush(stdout);
    return 0;
}
