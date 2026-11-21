/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Actual production sources; static state access only for exhaustion injection
   and observations. No alternate policy/allocator implementation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../kernel/arch/dreamcast/sound/snd_channel.c"
#include "../../kernel/arch/dreamcast/sound/snd_port.c"

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); assert(irq_level == 0); } while(0)

static void capacity(void) {
    int legacy[64];
    snd_port_t p = 123, stale, handles[64];
    snd_port_info_t info, saved;
    CHECK(snd_port_reserve(0, &p) == -1 && errno == EINVAL && p == 123);
    CHECK(snd_port_reserve(3, &p) == -1 && p == 123);
    CHECK(snd_port_reserve(1, NULL) == -1 && errno == EINVAL);
    CHECK(snd_port_query(0, &info) == -1 && errno == EINVAL);
    CHECK(snd_port_close(0) == -1 && errno == EINVAL);
    for(int i = 0; i < 63; ++i) {
        legacy[i] = snd_sfx_chn_alloc();
        CHECK(legacy[i] == i);
    }
    CHECK(snd_port_reserve(2, &p) == -1 && errno == ENOSPC && p == 123);
    CHECK(snd_channel_reserved() == (UINT64_MAX >> 1));
    CHECK(snd_port_reserve(1, &p) == 0);
    CHECK(snd_port_query(p, &info) == 0 && info.channels[0] == 63 && info.channels[1] == -1);
    CHECK(snd_sfx_chn_alloc() == -1 && errno == ENOSPC);
    CHECK(snd_port_close(p) == 0);
    stale = p;
    CHECK(snd_port_close(stale) == -1 && errno == EINVAL);
    saved = info;
    CHECK(snd_port_query(stale, &info) == -1 && memcmp(&info, &saved, sizeof(info)) == 0);
    /* Noncontiguous pair; release must not disturb the intervening owner. */
    snd_sfx_chn_free(17);
    CHECK(snd_port_reserve(2, &p) == 0 && p != stale);
    CHECK(snd_port_query(stale, &info) == -1);
    CHECK(snd_port_query(p, &info) == 0 && info.channel_count == 2 && info.channels[0] == 17 && info.channels[1] == 63);
    CHECK(snd_port_query(p, NULL) == -1 && errno == EINVAL);
    CHECK(snd_port_close(p) == 0);
    CHECK(snd_channel_reserved() == ((UINT64_MAX >> 1) & ~(UINT64_C(1) << 17)));
    for(int i = 0; i < 63; ++i)
        if(i != 17) snd_sfx_chn_free(legacy[i]);
    CHECK(snd_channel_reserved() == 0);
    snd_sfx_chn_free(-1);
    CHECK(errno == EINVAL && snd_channel_reserved() == 0);
    snd_sfx_chn_free(64);
    CHECK(errno == EINVAL && snd_channel_reserved() == 0);
    for(unsigned i = 0; i < 64; ++i)
        CHECK(snd_port_reserve(1, &handles[i]) == 0);
    CHECK(snd_port_reserve(1, &p) == -1 && errno == ENOSPC);
    for(unsigned i = 0; i < 64; ++i)
        CHECK(snd_port_close(handles[i]) == 0);
    for(unsigned i = 0; i < 32; ++i)
        CHECK(snd_port_reserve(2, &handles[i]) == 0);
    CHECK(snd_port_reserve(2, &p) == -1 && errno == ENOSPC);
    for(unsigned i = 0; i < 32; ++i)
        CHECK(snd_port_close(handles[i]) == 0);
    CHECK(snd_channel_reserved() == 0);
}

static void priority(void) {
    for(unsigned current = 0; current < 16; ++current) {
        for(unsigned incoming = 0; incoming < 16; ++incoming) {
            snd_port_t p;
            snd_port_transaction_t t, prior, untouched = { 123, 456 };
            port_slot_t *slot;
            CHECK(snd_port_reserve(2, &p) == 0);
            slot = &ports[p & 63];
            CHECK(snd_port_begin(p, current, &t) == 1); /* inactive */
            CHECK(snd_port_finish(t, false) == 0 && !slot->active);
            CHECK(snd_port_begin(p, current, &t) == 1);
            CHECK(snd_port_finish(t, true) == 0 && slot->active && slot->priority == current);
            prior = t;
            CHECK(snd_port_finish(prior, true) == -1 && errno == EINVAL);
            bool accepted = !current || !incoming || incoming >= current;
            t = untouched;
            CHECK(snd_port_begin(p, incoming, &t) == (accepted ? 1 : 0));
            CHECK(slot->priority == current);
            if(accepted) {
                uint64_t mask = snd_channel_reserved();
                CHECK(snd_port_close(p) == -1 && errno == EBUSY && snd_channel_reserved() == mask);
                CHECK(snd_port_begin(p, 0, &untouched) == -1 && errno == EBUSY && untouched.port == 123 && untouched.serial == 456);
                CHECK(snd_port_finish(prior, true) == -1 && errno == EINVAL && slot->pending);
                CHECK(snd_port_finish(t, false) == 0 && slot->priority == current);
                prior = t;
                CHECK(snd_port_begin(p, incoming, &t) == 1);
                CHECK(snd_port_finish(prior, true) == -1 && slot->pending);
                CHECK(snd_port_finish(t, true) == 0 && slot->priority == incoming);
            }
            else {
                CHECK(!slot->pending && t.port == 123 && t.serial == 456);
            }
            CHECK(snd_port_begin(p, 16, &t) == -1 && errno == EINVAL);
            CHECK(snd_port_begin(p, 0, NULL) == -1 && errno == EINVAL);
            CHECK(snd_port_close(p) == 0);
            CHECK(snd_port_begin(p, 0, &t) == -1 && errno == EINVAL);
            CHECK(snd_port_finish(prior, true) == -1 && errno == EINVAL);
        }
    }
}

static void exhaustion(void) {
    snd_port_t p, old;
    snd_port_transaction_t t;
    CHECK(snd_port_reserve(1, &p) == 0);
    ports[p & 63].serial = UINT32_MAX - 1;
    CHECK(snd_port_begin(p, 1, &t) == 1 && t.serial == UINT32_MAX);
    CHECK(snd_port_finish(t, false) == 0);
    CHECK(snd_port_begin(p, 1, &t) == -1 && errno == EOVERFLOW);
    CHECK(snd_port_close(p) == 0);
    ports[0].generation = PORT_GENERATION_MAX - 1;
    CHECK(snd_port_reserve(1, &p) == 0 && (p >> 6) == PORT_GENERATION_MAX);
    old = p;
    CHECK(snd_port_close(p) == 0);
    CHECK(snd_port_reserve(1, &p) == 0 && (p & 63) == 1);
    CHECK(snd_port_close(old) == -1 && errno == EINVAL);
    CHECK(snd_port_close(p) == 0);
    for(unsigned i = 0; i < 64; ++i)
        ports[i].generation = PORT_GENERATION_MAX;
    old = p;
    CHECK(snd_port_reserve(1, &p) == -1 && errno == EOVERFLOW && p == old);
    CHECK(snd_channel_reserved() == 0);
    CHECK(snd_sfx_chn_alloc() == 0); /* handle exhaustion does not leak channels */
    snd_sfx_chn_free(0);
}

static void source_limits(void) {
    snd_port_t music, shot, unused = 0;
    snd_port_info_t info;
    int pcm[2] = {-1, -1};

    CHECK(snd_voice_set_limits(&(snd_voice_limits_t){64, 1, 0}) == -1 && errno == EINVAL);
    CHECK(snd_voice_set_limits(&(snd_voice_limits_t){0, 0, 17}) == -1 && errno == EINVAL);
    CHECK(snd_voice_set_limits(&(snd_voice_limits_t){64, 0, 0}) == 0);
    CHECK(snd_channel_shot_unavailable() == UINT64_MAX);
    CHECK(snd_port_reserve_music(&music) == 0);
    CHECK(snd_port_reserve(1, &unused) == -1 && errno == ENOSPC && !unused);
    CHECK(snd_voice_set_limits(NULL) == -1 && errno == EBUSY);
    CHECK(snd_port_close(music) == 0);

    CHECK(snd_voice_set_limits(&(snd_voice_limits_t){1, 2, 2}) == 0);
    CHECK(snd_channel_shot_unavailable() == ~(UINT64_C(6)));
    CHECK(snd_port_reserve_music(&music) == 0);
    CHECK(snd_port_query(music, &info) == 0 && info.channels[0] == 0);
    CHECK(snd_port_reserve_music(&unused) == -1 && errno == ENOSPC && !unused);
    CHECK(snd_port_reserve(2, &shot) == 0);
    CHECK(snd_port_query(shot, &info) == 0 && info.channels[0] == 1 && info.channels[1] == 2);
    CHECK(snd_sfx_chn_alloc() == -1 && errno == ENOSPC);
    int irq = irq_disable();
    int paired = snd_channel_reserve(SND_CHANNEL_CLASS_PCM, 2, pcm);
    int denied = snd_channel_reserve(SND_CHANNEL_CLASS_PCM, 2, pcm);
    irq_restore(irq);
    CHECK(paired == 0 && denied == -1 && errno == ENOSPC && pcm[0] == 3 && pcm[1] == 4);
    CHECK(snd_voice_set_limits(NULL) == -1 && errno == EBUSY);
    snd_sfx_chn_free(pcm[0]);
    snd_sfx_chn_free(pcm[1]);
    CHECK(snd_port_close(shot) == 0 && snd_port_close(music) == 0);
    CHECK(snd_voice_set_limits(NULL) == 0 && !snd_channel_shot_unavailable());
}

int main(void) {
    capacity();
    priority();
    source_limits();
    exhaustion();
    printf("sound-port-test: PASS (%u checks)\n", checks);
    return 0;
}
