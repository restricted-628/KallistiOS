/* Original experimental completion example. Samples remain allocated forever.
   LP observation is not physically qualified end attribution or memory reuse. */
#include <kos.h>
#include <dc/sound/playback.h>
#include <errno.h>
#include <stdio.h>
static int16_t waveform[16384];
static snd_channel_config_t config;
static int finish(snd_port_operation_t *op) {
    snd_channel_observation_t o;
    if(snd_port_playback_poll(op, 1000, &o) < 0 || o.result != SND_OBSERVATION_APPLIED)
        return -1;
    return snd_port_playback_consume(op, 1000);
}
static int start(snd_port_t p, unsigned count, unsigned rate, int loop,
                 unsigned priority, snd_port_operation_t *op) {
    config.sample_count = config.loop_end = count;
    config.sample_rate = rate; config.loop_enabled = loop;
    return snd_port_playback_start(p, &config, priority, op);
}
static int wait_end(snd_port_t p, uint32_t token) {
    snd_port_playback_state_t s;
    uint64_t deadline = timer_ms_gettime64() + 1500;
    unsigned polls = 0;
    do {
        int rc = snd_port_playback_get_state(p, 100, &s);
        if(rc < 0) { printf("end query rc=%d errno=%d expected=%lu\n",rc,errno,(unsigned long)token); return -1; }
        if(!polls++) printf("end query ch=%lu start=%lu expected=%lu flags=%lu key=%lu pos=%lu state=%lu\n",
            (unsigned long)s.channel,(unsigned long)s.start_token,(unsigned long)token,
            (unsigned long)s.flags,(unsigned long)s.key_on,(unsigned long)s.position,(unsigned long)s.state);
        if(s.start_token != token) return -1;
        if(s.flags & SND_PLAYBACK_END_SEEN) return 0;
        thd_sleep(10);
    } while(timer_ms_gettime64() < deadline);
    printf("end query timed out flags=%lu key=%lu pos=%lu\n",(unsigned long)s.flags,
           (unsigned long)s.key_on,(unsigned long)s.position);
    return -1;
}
#define REQUIRE(x) do { if(!(x)) { printf("completion failure line=%d errno=%d\n", __LINE__, errno); goto fail; } } while(0)
int main(int argc, char **argv) {
    snd_port_t a, b, reopened;
    snd_port_info_t ai, bi, ri;
    snd_port_operation_t ao, bo, saved = {99,88,77};
    snd_port_playback_state_t s;
    snd_driver_status_t driver;
    (void)argc; (void)argv;
    REQUIRE(snd_init() == 0);
    REQUIRE(snd_driver_get_status(&driver, 1000) == 0);
    REQUIRE(driver.features & SND_DRIVER_FEATURE_COMPLETION);
    printf("Experimental completion firmware=%lx features=%lx\n",
           (unsigned long)driver.firmware_version, (unsigned long)driver.features);
    config.sample_address = snd_mem_malloc(sizeof(waveform));
    REQUIRE(config.sample_address);
    for(unsigned i = 0; i < 16384; ++i) {
        unsigned t = i % 128;
        waveform[i] = (int16_t)((t < 64 ? (int)t : 128 - (int)t) * 32 - 1024);
    }
    spu_memload(config.sample_address, waveform, sizeof(waveform));
    config.volume = 80; config.pan = 128; config.envelope.attack_rate = 31;
    config.envelope.release_rate = 0; config.routing.direct_level = 15;
    REQUIRE(snd_port_reserve(1, &a) == 0 && snd_port_reserve(1, &b) == 0);
    REQUIRE(snd_port_query(a, &ai) == 0 && snd_port_query(b, &bi) == 0);
    REQUIRE(ai.channels[0] != bi.channels[0]);
    REQUIRE(snd_port_playback_attach(a) == 0 && snd_port_playback_attach(b) == 0);
    REQUIRE(snd_port_playback_get_state(a, 100, &s) == -1 && errno == EAGAIN);
    REQUIRE(start(a, 32, 44100, 0, 15, &ao) == 1);
    REQUIRE(start(b, 4096, 22050, 0, 15, &bo) == 1);
    thd_sleep(300); /* Delayed polling, never a retirement criterion. */
    REQUIRE(finish(&ao) == 0 && finish(&bo) == 0);
    REQUIRE(wait_end(a, ao.token) == 0);
    /* No explicit end query on b: admission must itself refresh its end state. */
    REQUIRE(start(b, 2048, 22050, 0, 1, &bo) == 1 && finish(&bo) == 0);
    REQUIRE(wait_end(b, bo.token) == 0);
    REQUIRE(snd_port_playback_detach(a) == -1 && errno == EBUSY);
    printf("completion: delayed short end and alternating channels observed\n");
    REQUIRE(start(a, 2048, 22050, 1, 15, &ao) == 1 && finish(&ao) == 0);
    thd_sleep(250); /* Loop leaves LP unread by finite-end detection. */
    REQUIRE(snd_port_playback_get_state(a, 100, &s) == 0 && !s.flags);
    ao = saved;
    REQUIRE(start(a, 2048, 22050, 0, 1, &ao) == 0 && ao.token == saved.token);
    /* Replace the running loop; pre-keyon drain must not complete the new shot. */
    REQUIRE(start(a, 8192, 11025, 0, 15, &ao) == 1 && finish(&ao) == 0);
    REQUIRE(snd_port_playback_get_state(a, 100, &s) == 0 && s.start_token == ao.token &&
            !(s.flags & SND_PLAYBACK_END_SEEN));
    REQUIRE(wait_end(a, ao.token) == 0);
    config.volume = 60;
    REQUIRE(snd_port_playback_update(a, &config, SND_CHANNEL_UPDATE_VOLUME, &ao) == 0 && finish(&ao) == 0);
    REQUIRE(snd_port_playback_get_state(a, 100, &s) == 0 && (s.flags & SND_PLAYBACK_END_SEEN));
    printf("completion: loop excluded and replacement end retained across UPDATE\n");
    REQUIRE(snd_port_playback_stop(b, &bo) == 0 && finish(&bo) == 0);
    REQUIRE(snd_port_playback_detach(b) == 0 && snd_port_close(b) == 0);
    REQUIRE(snd_port_reserve(1, &reopened) == 0 && snd_port_query(reopened, &ri) == 0);
    REQUIRE(ri.channels[0] == bi.channels[0] && snd_port_playback_attach(reopened) == 0);
    REQUIRE(snd_port_playback_get_state(reopened, 100, &s) == -1 && errno == EAGAIN);
    REQUIRE(snd_port_playback_stop(reopened, &bo) == 0 && finish(&bo) == 0);
    REQUIRE(snd_port_playback_get_state(reopened, 100, &s) == -1 && errno == EAGAIN);
    REQUIRE(snd_port_playback_detach(reopened) == 0 && snd_port_close(reopened) == 0);
    REQUIRE(start(a, 8192, 11025, 0, 15, &ao) == 1 && finish(&ao) == 0);
    REQUIRE(snd_channel_stop(ai.channels[0]) == 0); /* Deliberate legacy interference. */
    REQUIRE(snd_port_playback_get_state(a, 100, &s) == 0 && s.state == SND_PLAYBACK_INTERFERED &&
            !(s.flags & SND_PLAYBACK_END_SEEN));
    REQUIRE(snd_port_playback_stop(a, &ao) == 0 && finish(&ao) == 0);
    REQUIRE(snd_port_playback_detach(a) == 0 && snd_port_close(a) == 0);
    printf("COMPLETION EXPERIMENT PASS: software policy only; all samples retained\n");
    for(;;) thd_sleep(1000);
fail:
    printf("COMPLETION EXPERIMENT FAIL: retain samples and live ownership\n");
    for(;;) thd_sleep(1000);
}
