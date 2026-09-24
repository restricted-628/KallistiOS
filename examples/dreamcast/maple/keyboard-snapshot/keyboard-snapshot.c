/* KallistiOS ##version##

   Maple keyboard metadata and coherent-snapshot validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <dc/maple/keyboard.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static void finish_test(void) __attribute__((noreturn));
static void finish_test(void) {
    fflush(stdout);

    for(;;)
        thd_sleep(1000);
}

static int wait_for_snapshot(maple_device_t *dev, kbd_state_t *snapshot,
                             uint32_t after_sequence, uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    do {
        if(kbd_get_snapshot(dev, snapshot) == 0) {
            if(snapshot->sequence != after_sequence)
                return 0;
        }
        else if(errno != EAGAIN) {
            return -1;
        }

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;
    return -1;
}

static int validate_pressed_keys(const kbd_state_t *snapshot) {
    size_t i;

    for(i = 0; i < KBD_MAX_PRESSED_KEYS; ++i) {
        kbd_key_t key = snapshot->cond.keys[i];

        if(key == KBD_KEY_NONE)
            break;

        if(!snapshot->key_states[key].is_down)
            return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    kbd_state_t first = { 0 };
    kbd_state_t second = { 0 };
    kbd_info_t info = { 0 };
    maple_device_t *dev;
    kbd_mods_t mods = { .raw = 0 };
    kbd_leds_t leds = { .raw = 0 };
    uint32_t descriptor = 0;
    uint32_t wire_descriptor;

    (void)argc;
    (void)argv;

    dbgio_dev_select("scif");
    dbgio_enable();
    printf("Keyboard snapshot validation\n");

    maple_wait_scan();
    dev = maple_enum_type(0, MAPLE_FUNC_KEYBOARD);

    if(!dev) {
        printf("FAIL: no keyboard device\n");
        finish_test();
    }

    errno = 0;
    if(kbd_get_info(NULL, &info) == 0 || errno != EINVAL ||
       kbd_get_snapshot(NULL, &first) == 0 || errno != EINVAL ||
       kbd_queue_pop(NULL, false) != KBD_QUEUE_END || errno != EINVAL) {
        printf("FAIL: invalid argument handling errno=%d\n", errno);
        finish_test();
    }

    if(!maple_dev_function_data(dev, MAPLE_FUNC_KEYBOARD, &descriptor) ||
       kbd_get_info(dev, &info) < 0) {
        printf("FAIL: descriptor query errno=%d\n", errno);
        finish_test();
    }

    wire_descriptor = __builtin_bswap32(descriptor);
    if(info.region != (kbd_region_t)((wire_descriptor >> 24) & 0xff) ||
       info.type != (kbd_type_t)((wire_descriptor >> 16) & 0xff) ||
       info.supported_leds.raw != ((wire_descriptor >> 8) & 0xff) ||
       info.led_control != (wire_descriptor & 0xff)) {
        printf("FAIL: descriptor decode %08lx region=%u type=%u leds=%02x ctrl=%02x\n",
               (unsigned long)descriptor, info.region, info.type,
               info.supported_leds.raw, info.led_control);
        finish_test();
    }

    if(kbd_key_to_ascii(KBD_KEY_A, KBD_REGION_US, mods, leds) != 'a') {
        printf("FAIL: base translation\n");
        finish_test();
    }

    mods.lshift = 1;

    if(kbd_key_to_ascii(KBD_KEY_A, KBD_REGION_US, mods, leds) != 'A' ||
       kbd_key_to_ascii(KBD_KEY_A, KBD_REGION_UNKNOWN, mods, leds) != 0 ||
       kbd_key_to_ascii(KBD_KEY_A, KBD_REGION_SE, mods, leds) != 0) {
        printf("FAIL: shifted or unsupported-region translation\n");
        finish_test();
    }

    kbd_set_repeat_timing(0, 0);

    if(wait_for_snapshot(dev, &first, 0, 1000) < 0 ||
       wait_for_snapshot(dev, &second, first.sequence, 1000) < 0 ||
       first.info.region != info.region || first.info.type != info.type ||
       first.info.supported_leds.raw != info.supported_leds.raw ||
       first.info.led_control != info.led_control ||
       validate_pressed_keys(&first) < 0 ||
       validate_pressed_keys(&second) < 0) {
        printf("FAIL: coherent sample errno=%d sequence=%lu->%lu\n", errno,
               (unsigned long)first.sequence,
               (unsigned long)second.sequence);
        finish_test();
    }

    printf("keyboard=%c%d descriptor=%08lx region=%u type=%u leds=%02x "
           "control=%02x sequence=%lu->%lu\n",
           'A' + dev->port, dev->unit, (unsigned long)descriptor,
           info.region, info.type, info.supported_leds.raw, info.led_control,
           (unsigned long)first.sequence, (unsigned long)second.sequence);
    printf("RESULT: PASS\n");
    finish_test();
}
