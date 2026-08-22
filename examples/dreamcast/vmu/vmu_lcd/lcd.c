/* KallistiOS ##version##

   VMU LCD conversion, orientation, and completion validation.
   Copyright (C) 2023 Paul Cercueil
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <kos.h>
#include <dc/maple/vmu.h>
#include <dc/vmu_fb.h>

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

static int validate_converter(void) {
    uint8_t pixels[VMU_SCREEN_WIDTH * VMU_SCREEN_HEIGHT] = { 0 };
    uint8_t bitmap[VMU_SCREEN_BITMAP_BYTES];

    pixels[0] = 0x08;

    if(vmu_lcd_pack_grayscale(bitmap, pixels, VMU_LCD_FLIP_NONE) < 0 ||
       bitmap[VMU_SCREEN_BITMAP_BYTES - 1] != 0x01)
        return -1;

    if(vmu_lcd_pack_grayscale(bitmap, pixels,
                              VMU_LCD_FLIP_HORIZONTAL) < 0 ||
       bitmap[(VMU_SCREEN_HEIGHT - 1) * 6] != 0x80)
        return -1;

    if(vmu_lcd_pack_grayscale(bitmap, pixels, VMU_LCD_FLIP_VERTICAL) < 0 ||
       bitmap[5] != 0x01)
        return -1;

    if(vmu_lcd_pack_grayscale(bitmap, pixels,
                              VMU_LCD_FLIP_HORIZONTAL |
                              VMU_LCD_FLIP_VERTICAL) < 0 ||
       bitmap[0] != 0x80)
        return -1;

    errno = 0;
    if(vmu_lcd_pack_grayscale(bitmap, pixels, (vmu_lcd_flip_t)4) == 0 ||
       errno != EINVAL)
        return -1;

    return 0;
}

static int validate_descriptor(void) {
    maple_device_t synthetic = { 0 };

    synthetic.valid = 1;
    synthetic.info.functions = MAPLE_FUNC_LCD;
    synthetic.info.function_data[0] = 0x00100500;

    if(vmu_lcd_is_compatible(&synthetic) != 1)
        return -1;

    synthetic.info.function_data[0] = 0;
    if(vmu_lcd_is_compatible(&synthetic) != 0)
        return -1;

    return 0;
}

static int wait_for_completion(maple_device_t *dev, uint32_t sequence,
                               uint64_t timeout_ms) {
    const uint64_t deadline = timer_ms_gettime64() + timeout_ms;
    vmu_lcd_status_t status;

    do {
        if(vmu_lcd_get_status(dev, &status) < 0)
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

static void build_asymmetric_test_image(vmufb_t *fb) {
    static const uint8_t marker[] = {
        0xf8, 0x88, 0x88, 0x88
    };

    vmufb_clear(fb);
    vmufb_paint_area(fb, 1, 1, 4, 8, marker);
    vmufb_print_string_into(fb, NULL, 9, 5, 36, 12, 0, "KOS\nLCD");
}

int main(int argc, char *argv[]) {
    vmufb_t framebuffer;
    vmu_lcd_direction_t direction;
    vmu_lcd_status_t status;
    maple_device_t *dev;
    int ready;
    int result;

    (void)argc;
    (void)argv;

    dbgio_dev_select("scif");
    dbgio_enable();
    printf("KOS VMU LCD validation\n");

    if(validate_converter() < 0 || validate_descriptor() < 0) {
        printf("FAIL: converter or descriptor test errno=%d\n", errno);
        finish_test();
    }

    maple_wait_scan();
    dev = maple_enum_type(0, MAPLE_FUNC_LCD);

    if(!dev) {
        printf("FAIL: no LCD device\n");
        finish_test();
    }

    ready = vmu_lcd_is_ready(dev);
    if(vmu_lcd_is_compatible(dev) != 1 || ready < 0 ||
       vmu_lcd_get_direction(dev, &direction) < 0 ||
       vmu_lcd_set_completion_handler(dev, completion_handler, NULL) < 0) {
        printf("FAIL: LCD metadata errno=%d\n", errno);
        finish_test();
    }

    printf("device=%c%d direction=%u ready=%d\n", 'A' + dev->port,
           dev->unit, direction, ready);

    build_asymmetric_test_image(&framebuffer);
    result = vmufb_present_ex(&framebuffer, dev);

    if(result != MAPLE_EOK || vmu_lcd_get_status(dev, &status) < 0 ||
       wait_for_completion(dev, status.submitted_sequence, 1000) < 0) {
        printf("FAIL: display completion result=%d errno=%d "
               "callback=(%lu,%d,%d)\n", result, errno,
               (unsigned long)callback_sequence, callback_result,
               callback_response);
        finish_test();
    }

    printf("RESULT: PASS\n");
    finish_test();
}
