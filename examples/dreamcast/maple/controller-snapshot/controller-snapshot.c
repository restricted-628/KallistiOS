/* KallistiOS ##version##

   Maple controller snapshot validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

/* This public KOS mask is deliberately independent of the helper
   under test; a byte-swapped implementation cannot satisfy both checks. */
_Static_assert(CONT_TYPE_STANDARD_CONTROLLER == 0xfe060f00u,
               "standard controller capability layout changed");

static volatile uint32_t callback_samples;
static volatile uint32_t callback_sequence;
static maple_device_t *expected_device;

/* This is an on-screen hardware/emulator validation program. Keep its result
   visible until the console is reset instead of returning into firmware. */
static void finish_test(void) __attribute__((noreturn));
static void finish_test(void) {
    fflush(stdout);

    for(;;)
        thd_sleep(1000);
}

static void sample_handler(maple_device_t *dev,
                           const cont_snapshot_t *snapshot,
                           void *user_data) {
    (void)user_data;

    if(dev == expected_device) {
        callback_sequence = snapshot->sequence;
        ++callback_samples;
    }
}

static int validate_function_data_mapping(void) {
    maple_device_t synthetic = { 0 };
    uint32_t data;

    synthetic.info.functions = MAPLE_FUNC_CONTROLLER | MAPLE_FUNC_MEMCARD |
                               MAPLE_FUNC_LCD | MAPLE_FUNC_CLOCK;
    synthetic.info.function_data[0] = 0x11223344;
    synthetic.info.function_data[1] = 0x55667788;
    synthetic.info.function_data[2] = 0x99aabbcc;

    if(!maple_dev_function_data(&synthetic, MAPLE_FUNC_CLOCK, &data) ||
       data != 0x11223344)
        return -1;

    if(!maple_dev_function_data(&synthetic, MAPLE_FUNC_LCD, &data) ||
       data != 0x55667788)
        return -1;

    if(!maple_dev_function_data(&synthetic, MAPLE_FUNC_MEMCARD, &data) ||
       data != 0x99aabbcc)
        return -1;

    if(maple_dev_function_data(&synthetic, MAPLE_FUNC_CONTROLLER, &data) ||
       maple_dev_function_data(&synthetic,
                               MAPLE_FUNC_CONTROLLER | MAPLE_FUNC_MEMCARD,
                               &data))
        return -1;

    return 0;
}

static int validate_connection_direction_mapping(void) {
    maple_device_t synthetic = { 0 };
    maple_connection_direction_t direction;

    synthetic.unit = 0;
    synthetic.info.connector_direction =
        MAPLE_CONNECTION_LEFT | (MAPLE_CONNECTION_RIGHT << 2);

    if(!maple_dev_connection_direction(&synthetic, 0, &direction) ||
       direction != MAPLE_CONNECTION_LEFT ||
       !maple_dev_connection_direction(&synthetic, 1, &direction) ||
       direction != MAPLE_CONNECTION_RIGHT)
        return -1;

    synthetic.unit = 1;

    for(unsigned int raw = 1; raw <= 8; raw <<= 1) {
        synthetic.info.connector_direction = raw;

        if(!maple_dev_connection_direction(&synthetic, 0, &direction) ||
           direction != (maple_connection_direction_t)__builtin_ctz(raw))
            return -1;
    }

    synthetic.info.connector_direction = 3;

    if(maple_dev_connection_direction(&synthetic, 0, &direction) ||
       maple_dev_connection_direction(&synthetic, 1, &direction))
        return -1;

    return 0;
}

static int validate_soft_reset_mapping(void) {
    maple_device_t synthetic = { 0 };
    cont_snapshot_t snapshot = { 0 };

    synthetic.info.functions = MAPLE_FUNC_CONTROLLER;
    synthetic.info.function_data[0] =
        CONT_CAPABILITY_X | CONT_CAPABILITY_Y;
    snapshot.state.buttons = CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START;
    snapshot.pressed = CONT_START;

    if(!cont_snapshot_is_soft_reset(&synthetic, &snapshot))
        return -1;

    snapshot.state.buttons &= ~CONT_X;

    if(cont_snapshot_is_soft_reset(&synthetic, &snapshot))
        return -1;

    synthetic.info.function_data[0] = 0;
    snapshot.state.buttons = CONT_A | CONT_B | CONT_START;

    if(!cont_snapshot_is_soft_reset(&synthetic, &snapshot))
        return -1;

    synthetic.info.function_data[0] = CONT_CAPABILITY_X;

    if(cont_snapshot_is_soft_reset(&synthetic, &snapshot))
        return -1;

    snapshot.pressed = 0;
    synthetic.info.function_data[0] = 0;

    return cont_snapshot_is_soft_reset(&synthetic, &snapshot) ? -1 : 0;
}

static int wait_for_snapshot(maple_device_t *dev, cont_snapshot_t *snapshot,
                             uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    do {
        if(cont_get_snapshot(dev, snapshot) == 0)
            return 0;

        if(errno != EAGAIN)
            return -1;

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;
    return -1;
}

int main(int argc, char *argv[]) {
    cont_snapshot_t first;
    cont_snapshot_t latest;
    maple_device_t *dev;
    uint32_t capabilities;
    uint64_t deadline;

    (void)argc;
    (void)argv;

    /* Keep results visible even when a direct ELF loader is selected as the
       automatic debug device and does not mirror output to its host console. */
    dbgio_dev_select("fb");
    dbgio_enable();
    printf("Maple controller snapshot validation\n");

    maple_wait_scan();

    if(maple_enum_dev(-1, 0) ||
       maple_enum_dev(MAPLE_PORT_COUNT, 0) ||
       maple_enum_dev(0, MAPLE_UNIT_COUNT) ||
       maple_enum_type(-1, MAPLE_FUNC_CONTROLLER)) {
        printf("FAIL: out-of-range enumeration was accepted\n");
        finish_test();
    }

    if(validate_function_data_mapping() < 0) {
        printf("FAIL: function-data mapping\n");
        finish_test();
    }

    if(validate_connection_direction_mapping() < 0) {
        printf("FAIL: connection-direction mapping\n");
        finish_test();
    }

    if(validate_soft_reset_mapping() < 0) {
        printf("FAIL: soft-reset mapping\n");
        finish_test();
    }

    if(cont_set_trigger_thresholds(
           CONT_TRIGGER_RELEASE_DEFAULT, CONT_TRIGGER_PRESS_DEFAULT) == 0 ||
       errno != EINVAL ||
       cont_set_trigger_thresholds(
           CONT_TRIGGER_PRESS_DEFAULT, CONT_TRIGGER_RELEASE_DEFAULT) < 0) {
        printf("FAIL: trigger-threshold validation\n");
        finish_test();
    }

    dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);

    if(!dev) {
        printf("FAIL: no controller detected\n");
        finish_test();
    }

    if(!maple_dev_function_data(dev, MAPLE_FUNC_CONTROLLER, &capabilities) ||
       capabilities != CONT_TYPE_STANDARD_CONTROLLER ||
       cont_is_type(dev, CONT_TYPE_STANDARD_CONTROLLER) != 1 ||
       cont_has_capabilities(dev, CONT_CAPABILITIES_STANDARD_BUTTONS) != 1) {
        printf("FAIL: live capability decoding\n");
        finish_test();
    }

    if(maple_enum_type_ex(0, MAPLE_FUNC_CONTROLLER,
                          CONT_CAPABILITIES_STANDARD_BUTTONS) != dev) {
        printf("FAIL: capability enumeration\n");
        finish_test();
    }

    if(wait_for_snapshot(dev, &first, 1000) < 0) {
        printf("FAIL: first snapshot errno=%d\n", errno);
        finish_test();
    }

    expected_device = dev;
    cont_set_sample_handler(sample_handler, NULL);
    deadline = timer_ms_gettime64() + 500;

    while(callback_samples < 2 && timer_ms_gettime64() < deadline)
        thd_pass();

    cont_set_sample_handler(NULL, NULL);

    if(callback_samples < 2 || wait_for_snapshot(dev, &latest, 100) < 0 ||
       latest.sequence == first.sequence ||
       callback_sequence > latest.sequence ||
       (latest.pressed & latest.released) ||
       (latest.pressed & ~latest.state.buttons) ||
       (latest.released & latest.state.buttons) ||
       (latest.state.rtrig >= CONT_TRIGGER_PRESS_DEFAULT &&
        !(latest.state.buttons & CONT_RTRIG_DIGITAL)) ||
       (latest.state.rtrig <= CONT_TRIGGER_RELEASE_DEFAULT &&
        (latest.state.buttons & CONT_RTRIG_DIGITAL)) ||
       (latest.state.ltrig >= CONT_TRIGGER_PRESS_DEFAULT &&
        !(latest.state.buttons & CONT_LTRIG_DIGITAL)) ||
       (latest.state.ltrig <= CONT_TRIGGER_RELEASE_DEFAULT &&
        (latest.state.buttons & CONT_LTRIG_DIGITAL))) {
        printf("FAIL: snapshot/callback sequencing samples=%lu first=%lu "
               "latest=%lu callback=%lu\n",
               (unsigned long)callback_samples,
               (unsigned long)first.sequence,
               (unsigned long)latest.sequence,
               (unsigned long)callback_sequence);
        finish_test();
    }

    printf("controller=%c%d capabilities=%08lx samples=%lu\n",
           'A' + dev->port, dev->unit, (unsigned long)capabilities,
           (unsigned long)callback_samples);
    printf("RESULT: PASS\n");
    finish_test();
}
