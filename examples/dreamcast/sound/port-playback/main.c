/* Original resident waveform; retained for the entire running demonstration.
   No sample-free or physical retirement assertion. Reset exits the demo. */
#include <kos.h>
#include <dc/sound/playback.h>
#include <errno.h>
#include <stdio.h>
static int16_t waveform[2048];
static int finish(snd_port_operation_t *op) {
    snd_channel_observation_t observation;
    if(snd_port_playback_poll(op, 1000, &observation) < 0 ||
       observation.result != SND_OBSERVATION_APPLIED)
        return -1;
    return snd_port_playback_consume(op, 1000);
}
int main(int argc, char **argv) {
    snd_port_t port;
    snd_port_operation_t operation;
    snd_channel_config_t config = {0};
    uint32_t address;
    (void)argc; (void)argv;
    if(snd_init() < 0) goto fail;
    address = snd_mem_malloc(sizeof(waveform));
    if(!address) goto fail;
    /* A low-amplitude original triangle wave. */
    for(unsigned i = 0; i < 2048; ++i) {
        unsigned phase = i % 128;
        waveform[i] = (int16_t)((phase < 64 ? (int)phase : 128 - (int)phase) * 64 - 2048);
    }
    spu_memload(address, waveform, sizeof(waveform));
    config.sample_address = address;
    config.sample_count = config.loop_end = 2048;
    config.sample_rate = 22050;
    config.loop_enabled = true;
    config.volume = 100;
    config.pan = 128;
    config.envelope.attack_rate = 31;
    config.envelope.release_rate = 31;
    config.routing.direct_level = 15;
    if(snd_port_reserve(1, &port) < 0 || snd_port_playback_attach(port) < 0 ||
       snd_port_playback_start(port, &config, 8, &operation) != 1 ||
       finish(&operation) < 0) goto fail;
    thd_sleep(250);
    config.volume = 70;
    if(snd_port_playback_update(port, &config, SND_CHANNEL_UPDATE_VOLUME, &operation) < 0 ||
       finish(&operation) < 0) goto fail;
    thd_sleep(250);
    if(snd_port_playback_stop(port, &operation) < 0 || finish(&operation) < 0 ||
       snd_port_playback_detach(port) < 0 || snd_port_close(port) < 0) goto fail;
    printf("PORT PLAYBACK PASS: START UPDATE STOP and software detach; sample retained\n");
    printf("Looped playback; no sample reuse proof. Reset to exit.\n");
    for(;;) thd_sleep(1000);
fail:
    printf("PORT PLAYBACK FAIL errno=%d; retain samples and any live attachment\n", errno);
    for(;;) thd_sleep(1000);
}
