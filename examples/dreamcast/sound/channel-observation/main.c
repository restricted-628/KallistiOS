/* Original observation smoke test. No sample allocation or retirement claim. */
#include <kos.h>
#include <dc/sound/observation.h>
#include <dc/sound/port.h>
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv) {
    snd_port_t port;
    snd_port_info_t info;
    snd_channel_receipt_t receipt;
    snd_channel_observation_t observation;
    (void)argc; (void)argv;
    if(snd_init() < 0 || snd_port_reserve(1, &port) < 0 ||
       snd_port_query(port, &info) < 0) {
        printf("OBSERVATION FAIL init errno=%d\n", errno);
        return 1;
    }
    unsigned channel = (unsigned)info.channels[0];
    if(snd_channel_observed_submit(channel, SND_OBSERVATION_STOP,
                                   NULL, 0, 0, &receipt) < 0) {
        if(errno == ENOTSUP) {
            printf("OBSERVATION UNSUPPORTED: firmware lacks optional capability\n");
            snd_port_close(port);
            return 0;
        }
        printf("OBSERVATION FAIL submit errno=%d\n", errno);
        return 1;
    }
    if(snd_channel_observed_poll(&receipt, 1000, &observation) < 0 ||
       observation.result != SND_OBSERVATION_APPLIED ||
       observation.attribution_kind != SND_OBSERVATION_ATTR_MANAGED ||
       observation.attribution_token != receipt.token) {
        printf("OBSERVATION FAIL managed errno=%d\n", errno);
        return 1;
    }
    /* A cooperative legacy command must invalidate current attribution without
       overwriting the retained processing result. The next FIFO challenge
       ensures the legacy STOP has been processed before this observation. */
    if(snd_channel_stop(channel) < 0 ||
       snd_channel_observed_poll(&receipt, 1000, &observation) < 0 ||
       observation.result != SND_OBSERVATION_APPLIED ||
       observation.attribution_kind != SND_OBSERVATION_ATTR_LEGACY ||
       observation.attribution_token != 0 ||
       snd_channel_observed_consume(&receipt, 1000) < 0) {
        printf("OBSERVATION FAIL legacy/consume errno=%d\n", errno);
        return 1;
    }
    if(snd_port_close(port) < 0) {
        printf("OBSERVATION FAIL close errno=%d\n", errno);
        return 1;
    }
    printf("OBSERVATION PASS: processing, fresh challenge, legacy attribution, ACK\n");
    return 0;
}
