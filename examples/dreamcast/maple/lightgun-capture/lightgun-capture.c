/* KallistiOS ##version##

   Maple light-gun capture validation.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <dc/maple/lightgun.h>
#include <dc/pvr.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static volatile uint32_t callback_count;
static volatile uint32_t callback_sequence;
static volatile uint32_t trigger_filter_count;

static void finish_test(void) __attribute__((noreturn));
static void finish_test(void) {
    fflush(stdout);

    for(;;)
        thd_sleep(1000);
}

static void capture_handler(const lightgun_snapshot_t *snapshot,
                            void *user_data) {
    (void)user_data;
    callback_sequence = snapshot->sequence;
    ++callback_count;
}

static int suppress_physical_trigger(maple_device_t *dev,
                                     const cont_snapshot_t *snapshot,
                                     void *user_data) {
    (void)dev;
    (void)snapshot;
    (void)user_data;
    ++trigger_filter_count;
    return 0;
}

static int wait_for_capture(lightgun_snapshot_t *snapshot,
                            uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    do {
        if(lightgun_get_snapshot(snapshot) == 0)
            return 0;

        if(errno != EAGAIN)
            return -1;

        thd_pass();
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;
    return -1;
}

static int wait_for_controller(maple_device_t *dev, cont_snapshot_t *snapshot,
                               uint32_t after_sequence,
                               uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;

    do {
        if(cont_get_snapshot(dev, snapshot) == 0) {
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
    lightgun_snapshot_t capture;
    cont_snapshot_t controller_before;
    cont_snapshot_t controller_after;
    maple_device_t *dev;
    uint32_t border_before;
    uint32_t blank_before;
    uint8_t port_mask;

    (void)argc;
    (void)argv;

    dbgio_dev_select("scif");
    dbgio_enable();
    printf("Light-gun capture validation\n");

    maple_wait_scan();
    dev = maple_enum_type(0, MAPLE_FUNC_LIGHTGUN);

    if(!dev || !(dev->info.functions & MAPLE_FUNC_CONTROLLER)) {
        printf("FAIL: no compound controller/light-gun device\n");
        finish_test();
    }

    if(lightgun_get_flash_color() != LIGHTGUN_FLASH_COLOR_DEFAULT ||
       lightgun_set_enabled_ports(0x80) == 0 || errno != EINVAL) {
        printf("FAIL: defaults or mask validation\n");
        finish_test();
    }

    port_mask = (uint8_t)BIT(dev->port);
    border_before = PVR_GET(PVR_BORDER_COLOR);
    blank_before = PVR_GET(PVR_VIDEO_CFG) & BIT(3);

    if(lightgun_request_capture(dev->port) == 0 || errno != EINVAL ||
       lightgun_set_enabled_ports(port_mask) < 0 ||
       lightgun_get_enabled_ports() != port_mask) {
        printf("FAIL: disabled-port admission\n");
        finish_test();
    }

    if(wait_for_controller(dev, &controller_before, 0, 1000) < 0) {
        printf("FAIL: controller snapshot before capture errno=%d\n", errno);
        finish_test();
    }

    lightgun_set_capture_handler(capture_handler, NULL);
    lightgun_set_trigger_filter(suppress_physical_trigger, NULL);

    if(lightgun_request_capture(dev->port) < 0 ||
       wait_for_capture(&capture, 2000) < 0) {
        printf("FAIL: capture did not complete errno=%d\n", errno);
        finish_test();
    }

    if(wait_for_controller(dev, &controller_after,
                           controller_before.sequence, 1000) < 0) {
        printf("FAIL: controller polling did not resume errno=%d\n", errno);
        finish_test();
    }

    lightgun_set_capture_handler(NULL, NULL);
    lightgun_set_trigger_filter(NULL, NULL);
    lightgun_set_enabled_ports(0);

    if(capture.port != dev->port || capture.x < 0 || capture.x > 0x3ff ||
       capture.y < 0 || capture.y > 0x3ff || callback_count != 1 ||
       trigger_filter_count == 0 ||
       callback_sequence != capture.sequence ||
       PVR_GET(PVR_BORDER_COLOR) != border_before ||
       (PVR_GET(PVR_VIDEO_CFG) & BIT(3)) != blank_before) {
        printf("FAIL: result/callback/resume port=%d x=%d y=%d cb=%lu\n",
               capture.port, capture.x, capture.y,
               (unsigned long)callback_count);
        finish_test();
    }

    printf("gun=%c0 raw=(%d,%d) capture=%lu controller=%lu->%lu\n",
           'A' + capture.port, capture.x, capture.y,
           (unsigned long)capture.sequence,
           (unsigned long)controller_before.sequence,
           (unsigned long)controller_after.sequence);
    printf("RESULT: PASS\n");
    finish_test();
}
