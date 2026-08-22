/* KallistiOS ##version##

   KOS vibration metadata, typed-effect, and completion validation.
   Copyright (C) 2004 SinisterTengu
   Copyright (C) 2008, 2023, 2025 Donald Haase
   Copyright (C) 2024 Daniel Fairchild
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>
#include <dc/maple/purupuru.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static volatile uint32_t callback_sequence;
static volatile int callback_result;
static volatile int callback_response;

static void completion_handler(maple_device_t *dev, int result, int response,
                               uint32_t sequence, void *user_data) {
    (void)dev;
    (void)user_data;

    /* Maple callbacks run in interrupt context. Publish only fixed-size state;
       printing and waiting remain in the application thread. */
    callback_result = result;
    callback_response = response;
    callback_sequence = sequence;
}

static void finish_test(void) __attribute__((noreturn));
static void finish_test(void) {
    fflush(stdout);

    for(;;)
        thd_sleep(1000);
}

static int validate_typed_effects(void) {
    purupuru_effect_config_t config = {
        .unit = 1,
        .power = 7,
        .frequency = 26,
        .cycles = 0,
        .continuous = false,
        .ramp = PURUPURU_RAMP_NONE
    };
    purupuru_effect_config_t decoded = { 0 };
    purupuru_effect_t effect = { .raw = 0 };
    maple_device_t synthetic = { 0 };
    purupuru_info_t info = { 0 };

    if(purupuru_effect_encode(&config, &effect) < 0 ||
       effect.raw != 0x001a7010 ||
       purupuru_effect_decode(&effect, &decoded) < 0 ||
       decoded.unit != config.unit || decoded.power != config.power ||
       decoded.frequency != config.frequency ||
       decoded.cycles != config.cycles ||
       decoded.continuous != config.continuous ||
       decoded.ramp != config.ramp) {
        return -1;
    }

    config.power = -7;
    config.frequency = 30;
    config.cycles = 8;
    config.ramp = PURUPURU_RAMP_DOWN;

    if(purupuru_effect_encode(&config, &effect) < 0 ||
       effect.raw != 0x081e8710 ||
       purupuru_effect_decode(&effect, &decoded) < 0 ||
       decoded.power != -7 || decoded.ramp != PURUPURU_RAMP_DOWN) {
        return -1;
    }

    synthetic.valid = 1;
    synthetic.info.functions = MAPLE_FUNC_PURUPURU;
    synthetic.info.function_data[0] = 0x00000101;

    if(purupuru_get_info(&synthetic, &info) < 0 || info.units != 1 ||
       info.simultaneous_units != 1) {
        return -1;
    }

    config.unit = 0;
    errno = 0;
    if(purupuru_effect_encode(&config, &effect) == 0 || errno != EINVAL)
        return -1;

    return 0;
}

static int wait_for_completion(maple_device_t *dev, uint32_t sequence,
                               uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    purupuru_status_t status;

    do {
        if(purupuru_get_status(dev, &status) < 0)
            return -1;

        if(!status.busy && status.completed_sequence == sequence) {
            if(status.result != MAPLE_EOK || callback_sequence != sequence ||
               callback_result != status.result ||
               callback_response != status.response) {
                errno = EPROTO;
                return -1;
            }

            return 0;
        }

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;
    return -1;
}

static int submit_and_wait(maple_device_t *dev, int submit_result) {
    purupuru_status_t status;

    if(submit_result != MAPLE_EOK || purupuru_get_status(dev, &status) < 0)
        return -1;

    return wait_for_completion(dev, status.submitted_sequence, 1000);
}

int main(int argc, char *argv[]) {
    purupuru_effect_config_t config = {
        .unit = 1,
        .power = 7,
        .frequency = 26,
        .cycles = 0,
        .continuous = false,
        .ramp = PURUPURU_RAMP_NONE
    };
    purupuru_effect_t effect;
    purupuru_info_t info;
    purupuru_direction_t direction;
    maple_device_t *dev;
    uint8_t unit;

    (void)argc;
    (void)argv;

    dbgio_dev_select("scif");
    dbgio_enable();
    printf("KOS vibration validation\n");

    if(validate_typed_effects() < 0) {
        printf("FAIL: typed effect or descriptor test errno=%d\n", errno);
        finish_test();
    }

    maple_wait_scan();
    dev = maple_enum_type(0, MAPLE_FUNC_PURUPURU);

    if(!dev) {
        printf("FAIL: no vibration device\n");
        finish_test();
    }

    if(purupuru_get_info(dev, &info) < 0 ||
       purupuru_get_direction(dev, &direction) < 0 ||
       purupuru_set_completion_handler(dev, completion_handler, NULL) < 0) {
        printf("FAIL: device metadata errno=%d\n", errno);
        finish_test();
    }

    printf("device=%c%d units=%u simultaneous=%u direction=%u\n",
           'A' + dev->port, dev->unit, info.units,
           info.simultaneous_units, direction);

    for(unit = 1; unit <= info.units; ++unit) {
        purupuru_unit_info_t unit_info;

        if(purupuru_get_unit_info(dev, unit, &unit_info) < 0) {
            printf("FAIL: unit %u metadata errno=%d\n", unit, errno);
            finish_test();
        }

        printf("unit=%u position=%u axis=%u power=%u continuous=%u "
               "directional=%u waveform=%u frequency=%u..%u mode=%u\n",
               unit_info.unit, unit_info.position, unit_info.axis,
               unit_info.variable_power, unit_info.continuous,
               unit_info.directional, unit_info.arbitrary_waveform,
               unit_info.minimum_frequency, unit_info.maximum_frequency,
               unit_info.frequency_mode);
    }

    if(submit_and_wait(dev, purupuru_set_autostop_time(dev, 1, 7)) < 0 ||
       purupuru_effect_encode(&config, &effect) < 0 ||
       submit_and_wait(dev, purupuru_rumble(dev, &effect)) < 0 ||
       submit_and_wait(dev, purupuru_stop(dev, 1)) < 0) {
        printf("FAIL: output completion errno=%d callback=(%lu,%d,%d)\n",
               errno, (unsigned long)callback_sequence, callback_result,
               callback_response);
        finish_test();
    }

    printf("RESULT: PASS\n");
    finish_test();
}
