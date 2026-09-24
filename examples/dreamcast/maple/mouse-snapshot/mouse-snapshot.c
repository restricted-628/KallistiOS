/* KallistiOS ##version##

   KOS Maple mouse metadata and coherent-snapshot validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <dc/maple/mouse.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static void finish_test(void) __attribute__((noreturn));
static void finish_test(void) {
    fflush(stdout);

    for(;;)
        thd_sleep(1000);
}

static int validate_descriptor_decode(void) {
    maple_device_t synthetic = { 0 };
    mouse_info_t info = { 0 };

    synthetic.valid = 1;
    synthetic.info.functions = MAPLE_FUNC_MOUSE;
    synthetic.info.function_data[0] = 0x5a070e00;

    if(mouse_get_info(&synthetic, &info) < 0 || info.reserved != 0 ||
       info.buttons != 0x0e || info.axes != 0x07 ||
       info.reserved2 != 0x5a) {
        return -1;
    }

    synthetic.info.functions = MAPLE_FUNC_CONTROLLER | MAPLE_FUNC_MOUSE;
    synthetic.info.function_data[0] = 0x11223344;
    synthetic.info.function_data[1] = 0x5a070e00;

    if(mouse_get_info(&synthetic, &info) < 0 || info.buttons != 0x0e ||
       info.axes != 0x07 || info.reserved2 != 0x5a) {
        return -1;
    }

    return 0;
}

static int wait_for_snapshot(maple_device_t *dev,
                             mouse_snapshot_t *snapshot,
                             uint32_t after_sequence,
                             uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    do {
        if(mouse_get_snapshot(dev, snapshot) == 0) {
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

int main(int argc, char *argv[]) {
    mouse_snapshot_t first = { 0 };
    mouse_snapshot_t second = { 0 };
    mouse_info_t info = { 0 };
    mouse_info_t invalid_info = { 0 };
    maple_device_t *dev;
    uint32_t descriptor = 0;

    (void)argc;
    (void)argv;

    dbgio_dev_select("scif");
    dbgio_enable();
    printf("KOS mouse snapshot validation\n");

    errno = 0;
    if(mouse_get_info(NULL, &invalid_info) == 0 || errno != EINVAL ||
       mouse_get_snapshot(NULL, &first) == 0 || errno != EINVAL ||
       validate_descriptor_decode() < 0) {
        printf("FAIL: argument or synthetic descriptor validation errno=%d\n",
               errno);
        finish_test();
    }

    maple_wait_scan();
    dev = maple_enum_type(0, MAPLE_FUNC_MOUSE);

    if(!dev) {
        printf("FAIL: no mouse device\n");
        finish_test();
    }

    if(!maple_dev_function_data(dev, MAPLE_FUNC_MOUSE, &descriptor) ||
       mouse_get_info(dev, &info) < 0 ||
       info.reserved != (descriptor & 0xff) ||
       info.buttons != ((descriptor >> 8) & 0xff) ||
       info.axes != ((descriptor >> 16) & 0xff) ||
       info.reserved2 != ((descriptor >> 24) & 0xff)) {
        printf("FAIL: live descriptor decode %08lx errno=%d\n",
               (unsigned long)descriptor, errno);
        finish_test();
    }

    if(wait_for_snapshot(dev, &first, 0, 1000) < 0 ||
       wait_for_snapshot(dev, &second, first.sequence, 1000) < 0) {
        printf("FAIL: coherent sample errno=%d sequence=%lu->%lu\n", errno,
               (unsigned long)first.sequence,
               (unsigned long)second.sequence);
        finish_test();
    }

    if(second.sequence == first.sequence + 1 &&
       (second.pressed !=
            (second.state.buttons & ~first.state.buttons) ||
        second.released !=
            (first.state.buttons & ~second.state.buttons))) {
        printf("FAIL: consecutive transition accounting\n");
        finish_test();
    }

    printf("mouse=%c%d descriptor=%08lx buttons=%02x axes=%02x "
           "sequence=%lu->%lu delta=(%d,%d,%d)\n",
           'A' + dev->port, dev->unit, (unsigned long)descriptor,
           info.buttons, info.axes, (unsigned long)first.sequence,
           (unsigned long)second.sequence, second.state.dx,
           second.state.dy, second.state.dz);
    printf("RESULT: PASS\n");
    finish_test();
}
