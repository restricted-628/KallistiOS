/* KallistiOS ##version##

   Buffered Maple microphone capture example.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <dc/maple.h>
#include <dc/maple/sip.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static uint8_t capture_buffer[16 * 1024];

int main(int argc, char **argv) {
    maple_device_t *microphone;
    sip_capture_t *capture;
    sip_stream_t *stream;
    sip_stream_status_t stream_status;
    int16_t samples[256];
    uint64_t total_samples = 0;
    int32_t peak = 0;
    int result;

    (void)argc;
    (void)argv;

    printf("KallistiOS ##version##\n\n");
    printf("Buffered microphone capture\n");

    microphone = maple_enum_type(0, MAPLE_FUNC_MICROPHONE);
    if(!microphone) {
        printf("No microphone is attached.\n");
        return 0;
    }

    if(sip_set_sample_type(microphone, SIP_SAMPLE_16BIT_SIGNED) != MAPLE_EOK ||
       sip_set_frequency(microphone, SIP_SAMPLE_11KHZ) != MAPLE_EOK ||
       sip_set_gain(microphone, SIP_DEFAULT_GAIN) != MAPLE_EOK) {
        printf("Unable to configure microphone.\n");
        return 1;
    }

    capture = sip_capture_create(microphone, capture_buffer,
                                 sizeof(capture_buffer));
    if(!capture) {
        printf("Unable to create capture: errno=%d\n", errno);
        return 1;
    }

    stream = sip_stream_open(capture, true);
    if(!stream) {
        printf("Unable to create reader: errno=%d\n", errno);
        sip_capture_destroy(capture);
        return 1;
    }

    result = sip_capture_start(capture, true);
    if(result != MAPLE_EOK) {
        printf("Unable to start capture: result=%d\n", result);
        sip_stream_close(stream);
        sip_capture_destroy(capture);
        return 1;
    }

    for(int frame = 0; frame < 300; ++frame) {
        ssize_t count = sip_stream_read(stream, samples,
                                        sizeof(samples) / sizeof(samples[0]));

        if(count < 0) {
            printf("Read failed: errno=%d\n", errno);
            break;
        }

        for(ssize_t i = 0; i < count; ++i) {
            int32_t amplitude = samples[i];

            if(amplitude < 0)
                amplitude = -amplitude;

            if(amplitude > peak)
                peak = amplitude;
        }

        total_samples += (uint64_t)count;
        thd_sleep(16);
    }

    result = sip_capture_stop(capture, true);
    if(result != MAPLE_EOK)
        printf("Capture stop returned %d.\n", result);

    if(sip_stream_get_status(stream, &stream_status) == 0) {
        printf("samples=%llu peak=%ld available=%lu lost=%llu overrun=%s\n",
               (unsigned long long)total_samples, (long)peak,
               (unsigned long)stream_status.available_samples,
               (unsigned long long)stream_status.lost_samples,
               stream_status.overrun ? "yes" : "no");
    }

    sip_stream_close(stream);
    sip_capture_destroy(capture);
    return 0;
}
