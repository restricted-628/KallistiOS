/* KallistiOS ##version##

   vmu.c
   Copyright (C) 2002, 2003 Megan Potter
   Copyright (C) 2008 Donald Haase
   Copyright (C) 2023, 2025 Falco Girgis
   Copyright (C) 2026 Joseph Black
 */

/*
   This module deals with the VMU.  It provides functionality for
   filesystem, LCD screen, buzzer, and date/time access.

   Thanks to Marcus Comstedt for VMU/Maple information.
 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <kos/thread.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/platform.h>
#include <kos/dbglog.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/math.h>
#include <dc/biosfont.h>
#include <dc/vmufs.h>
#include <kos/timer.h>

#define VMU_BLOCK_WRITE_RETRY_TIME  100     /* time to sleep until retrying a failed write */

/* vmu_state_t remains first so existing maple_dev_status() callers retain the
   original layout while LCD completion state stays driver-owned. */
typedef struct vmu_driver_status {
    vmu_state_t public;
    vmu_lcd_status_t lcd;
    vmu_lcd_completion_handler_t lcd_handler;
    void *lcd_handler_data;
    vmu_clock_status_t clock;
    vmu_clock_completion_handler_t clock_handler;
    void *clock_handler_data;
    bool clock_sync_waiting;
} vmu_driver_status_t;

static bool vmu_has_clock(const maple_device_t *dev) {
    return dev && dev->valid &&
           (dev->info.functions & MAPLE_FUNC_CLOCK);
}

/* Check the LCD function descriptor for one 192-byte, one-block, monochrome
   image using the standard horizontal/vertical ordering and polarity. */
static bool vmu_has_standard_lcd(const maple_device_t *dev) {
    uint32_t descriptor;

    if(!maple_dev_function_data(dev, MAPLE_FUNC_LCD, &descriptor))
        return false;

    /* Function-data words retain KOS's public capability-mask ordering. This
       descriptor is byte-oriented, so normalize it before decoding fields. */
    descriptor = __builtin_bswap32(descriptor);
    return ((descriptor >> 24) & 0xff) == 0 &&
           ((descriptor >> 16) & 0xff) == 5 &&
           ((descriptor >> 12) & 0x0f) == 1 &&
           ((descriptor >> 6) & 0x03) == 0 &&
           ((descriptor >> 5) & 0x01) == 0;
}

static uint32_t next_lcd_sequence(uint32_t sequence) {
    if(++sequence == 0)
        sequence = 1;

    return sequence;
}

static uint32_t next_clock_sequence(uint32_t sequence) {
    if(++sequence == 0)
        sequence = 1;

    return sequence;
}

/* VMU's raw condition data: 0 = PRESSED, 1 = RELEASED */
typedef struct vmu_cond {
    uint8_t raw_buttons;
    uint8_t dummy[3];
} vmu_cond_t;

_Static_assert(sizeof(vmu_clock_time_t) == 8,
               "Maple clock payload must occupy two words");

/* The clock payload uses Monday 0 through Sunday 6 on the wire. KOS exposes
   the standard C/POSIX Sunday-zero convention and translates at this edge. */
typedef struct vmu_datetime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
} vmu_datetime_t;

_Static_assert(sizeof(vmu_datetime_t) == 8,
               "Maple clock wire payload must occupy two words");

static bool vmu_clock_is_leap_year(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint8_t vmu_clock_calculate_weekday(uint16_t year, uint8_t month,
                                           uint8_t day);

static bool vmu_clock_time_fields_valid(const vmu_clock_time_t *time,
                                        bool validate_weekday) {
    static const uint8_t days_in_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    uint8_t limit;

    if(!time || time->year == 0 || time->year > 9999 ||
       time->month == 0 || time->month > 12 || time->day == 0 ||
       time->hour > 23 || time->minute > 59 || time->second > 59 ||
       (validate_weekday && time->weekday > 6))
        return false;

    limit = days_in_month[time->month - 1];
    if(time->month == 2 && vmu_clock_is_leap_year(time->year))
        ++limit;

    if(time->day > limit)
        return false;

    return !validate_weekday ||
           time->weekday == vmu_clock_calculate_weekday(
                                time->year, time->month, time->day);
}

/* Gregorian weekday, expressed as Sunday 0 through Saturday 6. */
static uint8_t vmu_clock_calculate_weekday(uint16_t year, uint8_t month,
                                           uint8_t day) {
    static const uint8_t offsets[] = {
        0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
    };
    uint32_t adjusted_year = year;

    if(month < 3)
        --adjusted_year;

    return (uint8_t)((adjusted_year + adjusted_year / 4 -
                      adjusted_year / 100 + adjusted_year / 400 +
                      offsets[month - 1] + day) % 7);
}

int vmu_clock_time_is_valid(const vmu_clock_time_t *time) {
    if(!time) {
        errno = EINVAL;
        return -1;
    }

    return vmu_clock_time_fields_valid(time, true);
}

static void vmu_datetime_to_tm(const vmu_clock_time_t *dt, struct tm *bt) {
    bt->tm_sec  = dt->second;
    bt->tm_min  = dt->minute;
    bt->tm_hour = dt->hour;
    bt->tm_mday = dt->day;
    bt->tm_mon  = dt->month - 1;
    bt->tm_year = dt->year - 1900;
    bt->tm_wday = dt->weekday;
    bt->tm_isdst = -1;
}

static void vmu_datetime_from_tm(vmu_clock_time_t *dt, const struct tm *bt) {
    dt->second  = bt->tm_sec;
    dt->minute  = bt->tm_min;
    dt->hour    = bt->tm_hour;
    dt->day     = bt->tm_mday;
    dt->month   = bt->tm_mon + 1;
    dt->year    = bt->tm_year + 1900;
    dt->weekday = bt->tm_wday;
}

static void vmu_datetime_from_clock_time(vmu_datetime_t *wire,
                                         const vmu_clock_time_t *time) {
    wire->year = time->year;
    wire->month = time->month;
    wire->day = time->day;
    wire->hour = time->hour;
    wire->minute = time->minute;
    wire->second = time->second;
    wire->weekday = time->weekday ? time->weekday - 1 : 6;
}

static void vmu_datetime_to_clock_time(vmu_clock_time_t *time,
                                       const vmu_datetime_t *wire) {
    time->year = wire->year;
    time->month = wire->month;
    time->day = wire->day;
    time->hour = wire->hour;
    time->minute = wire->minute;
    time->second = wire->second;
    time->weekday = 0;
}

static void vmu_poll_reply(maple_state_t *st, maple_frame_t *frm) {
    (void)st;

    maple_response_t   *resp;
    uint32_t           *respbuf;
    vmu_cond_t         *raw;
    vmu_driver_status_t *status;
    vmu_state_t        *cooked;

    /* Unlock the frame now (it's ok, we're in an IRQ) */
    maple_frame_unlock(frm);

    /* Make sure we got a valid response */
    resp = (maple_response_t *)frm->recv_buf;

    if(resp->response != MAPLE_RESPONSE_DATATRF)
        return;

    respbuf = (uint32_t *)resp->data;

    if(respbuf[0] != MAPLE_FUNC_CLOCK)
        return;

    if(!frm->dev)
        return;

    /* Verify the size of the frame and grab a pointer to it */
    assert(sizeof(vmu_cond_t) == ((resp->data_len - 1) * sizeof(uint32_t)));
    raw = (vmu_cond_t *)(respbuf + 1);

    /* Fill the "nice" struct from the raw data */
    status = frm->dev->status;
    cooked = &status->public;
    /* Copy over current button states to previous states. */
    cooked->buttons.previous = cooked->buttons.current;
    /* Invert raw struct as nice struct */
    cooked->buttons.current.raw = ~(raw->raw_buttons);

    /* Check to see if the VMU is upside-down in the controller and readjust
       its directional buttons accordingly. */
    const maple_device_t *cont = maple_enum_dev(frm->dev->port, 0);

    if(cont && (cont->info.functions & MAPLE_FUNC_CONTROLLER) &&
       (frm->dev->info.connector_direction == cont->info.connector_direction)) {
        cooked->buttons.current.raw =
            (cooked->buttons.current.raw & 0xf0)      |
            (cooked->buttons.current.dpad_up    << 1) | /* down */
            (cooked->buttons.current.dpad_down  << 0) | /* up */
            (cooked->buttons.current.dpad_left  << 3) | /* right */
            (cooked->buttons.current.dpad_right << 2);  /* left */
    }
}

static int vmu_poll(maple_device_t *dev) {
    /* Only query for button input on the front VMU of each controller 
       AND the device actually has the functionality. */
    if((dev->unit == 1) && vmu_has_clock(dev)) {
        if(maple_frame_trylock(&dev->frame) < 0)
            return 0;

        maple_frame_init(&dev->frame);
        dev->frame.send_buf[0] = MAPLE_FUNC_CLOCK;
        dev->frame.cmd = MAPLE_COMMAND_GETCOND;
        dev->frame.dst_port = dev->port;
        dev->frame.dst_unit = dev->unit;
        dev->frame.length = 1;
        dev->frame.callback = vmu_poll_reply;
        maple_queue_frame(&dev->frame);
    }

    return 0;
}

static void vmu_periodic(maple_driver_t *drv) {
    maple_driver_foreach(drv, vmu_poll);
}

static int vmu_attach(maple_driver_t *driver, maple_device_t *dev) {
    vmu_driver_status_t *status = dev->status;

    (void)driver;
    status->lcd.result = MAPLE_EOK;
    status->lcd.response = MAPLE_RESPONSE_NONE;
    status->clock.result = MAPLE_EOK;
    status->clock.response = MAPLE_RESPONSE_NONE;
    status->clock.operation = VMU_CLOCK_OPERATION_NONE;
    return 0;
}

/* Device Driver Struct */
static maple_driver_t vmu_drv = {
    .functions = MAPLE_FUNC_MEMCARD | MAPLE_FUNC_LCD | MAPLE_FUNC_CLOCK,
    .name = "VMU Driver",
    .status_size = sizeof(vmu_driver_status_t),
    .attach = vmu_attach
};

/* Add the VMU to the driver chain */
void vmu_init(void) {
    maple_driver_reg(&vmu_drv);
}

void vmu_shutdown(void) {
    maple_driver_unreg(&vmu_drv);
}

/* Dynamically add the periodic polling callback to the driver when button input is enabled. */
void vmu_set_buttons_enabled(int enable) {
    vmu_drv.periodic = enable ? vmu_periodic : NULL;
}

/* Determine whether polling for button input is enabled or not by presence of periodic callback. */
int vmu_get_buttons_enabled(void) {
    return !!vmu_drv.periodic;
}

int vmu_has_241_blocks(maple_device_t *dev) {
    vmu_root_t root;

    if(vmufs_root_read(dev, &root) < 0)
        return -1;

    if(root.blk_cnt == 241)
        return 1;

    return 0;
}

int vmu_toggle_241_blocks(maple_device_t *dev, int enable) {
    vmu_root_t root;

    if(vmufs_root_read(dev, &root) < 0)
        return -1;

    root.blk_cnt = (enable != 0) ? 241 : 200;

    if(vmufs_root_write(dev, &root) < 0)
        return -1;

    return 0;
}

int vmu_use_custom_color(maple_device_t *dev, int enable) {
    vmu_root_t root;

    if(vmufs_root_read(dev, &root) < 0)
        return -1;

    /* 1 - Enables the use of the custom color. 0 - Disables */
    root.use_custom = (enable != 0) ? 1 : 0;

    if(vmufs_root_write(dev, &root) < 0)
        return -1;

    return 0;
}

/* The custom color is used while navigating the Dreamcast's file manager.
   You set the RGBA parameters, each with valid range of 0-255 */
int vmu_set_custom_color(maple_device_t *dev, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    vmu_root_t root;

    if(vmufs_root_read(dev, &root) < 0)
        return -1;

    /* 1 - Enables the use of the custom color. 0 - Disables */
    root.use_custom = 1;
    root.custom_color[0] = blue;
    root.custom_color[1] = green;
    root.custom_color[2] = red;
    root.custom_color[3] = alpha;

    if(vmufs_root_write(dev, &root) < 0)
        return -1;

    return 0;
}

/* The icon shape is used while navigating the BIOS menu. The values
   for icon_shape are listed in the biosfont.h and start with
   BFONT_ICON_VMUICON. */
int vmu_set_icon_shape(maple_device_t *dev, uint8_t icon_shape) {
    vmu_root_t root;

    if(KOS_PLATFORM_IS_NAOMI)
        return -1;

    if(icon_shape < BFONT_ICON_VMUICON || icon_shape > BFONT_ICON_EMBROIDERY)
        return -1;

    if(vmufs_root_read(dev, &root) < 0)
        return -1;

    /* Valid value range is 0-123 and starts with BFONT_ICON_VMUICON which
       has a value of 5.  This is because we can't use the first 5 icons
       found in the bios so we must subtract 5 */
    root.icon_shape = icon_shape - BFONT_ICON_VMUICON;

    if(vmufs_root_write(dev, &root) < 0)
        return -1;

    return 0;
}

/* These interfaces will probably change eventually, but for now they
   can stay the same */

/* Set the tone to be generated by the VMU's speaker.
   Only last two bytes are used. This might necessitate
   refactoring as the clock is a separate device from
   the screen and storage. */
int vmu_beep_raw(maple_device_t *dev, uint32_t beep) {
    assert(dev);

    /* The clock function owns tone generation, independently of LCD/storage. */
    if(!vmu_has_clock(dev))
        return MAPLE_EINVALID;

    /* Lock the frame */
    if(maple_frame_trylock(&dev->frame) < 0)
        return MAPLE_EAGAIN;

    /* Reset the frame */
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CLOCK;
    dev->frame.send_buf[1] = beep;
    dev->frame.cmd = MAPLE_COMMAND_SETCOND;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = NULL;
    maple_queue_frame(&dev->frame);

    return MAPLE_EOK;
}

int vmu_beep_waveform(maple_device_t *dev, uint8_t period1, uint8_t duty_cycle1, uint8_t period2, uint8_t duty_cycle2) {
    const uint32_t raw_beep = (((period2 - duty_cycle2) << 24) | (period2 << 16) |
                               ((period1 - duty_cycle1) <<  8) | (period1));

    return vmu_beep_raw(dev, raw_beep);
}

int vmu_lcd_is_compatible(const maple_device_t *dev) {
    bool compatible;
    irq_mask_t irq;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !(dev->info.functions & MAPLE_FUNC_LCD)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    compatible = vmu_has_standard_lcd(dev);
    irq_restore(irq);
    return compatible;
}

int vmu_lcd_get_direction(const maple_device_t *dev,
                          vmu_lcd_direction_t *direction) {
    static const vmu_lcd_direction_t relative[4][4] = {
        { VMU_LCD_DIRECTION_FLIPPED, VMU_LCD_DIRECTION_NORMAL,
          VMU_LCD_DIRECTION_LEFT, VMU_LCD_DIRECTION_RIGHT },
        { VMU_LCD_DIRECTION_NORMAL, VMU_LCD_DIRECTION_FLIPPED,
          VMU_LCD_DIRECTION_RIGHT, VMU_LCD_DIRECTION_LEFT },
        { VMU_LCD_DIRECTION_RIGHT, VMU_LCD_DIRECTION_LEFT,
          VMU_LCD_DIRECTION_FLIPPED, VMU_LCD_DIRECTION_NORMAL },
        { VMU_LCD_DIRECTION_LEFT, VMU_LCD_DIRECTION_RIGHT,
          VMU_LCD_DIRECTION_NORMAL, VMU_LCD_DIRECTION_FLIPPED }
    };
    maple_connection_direction_t parent_direction;
    maple_connection_direction_t child_direction;
    const maple_device_t *parent;
    irq_mask_t irq;

    if(!dev || !direction) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !(dev->info.functions & MAPLE_FUNC_LCD)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    parent = maple_enum_dev(dev->port, 0);

    /* Root peripherals publish directions for their first two expansion
       sockets. Attached devices publish their own one-hot direction. */
    if(!parent || dev->unit < 1 || dev->unit > 2 ||
       !maple_dev_connection_direction(parent, dev->unit - 1,
                                       &parent_direction) ||
       !maple_dev_connection_direction(dev, 0, &child_direction)) {
        irq_restore(irq);
        errno = EPROTO;
        return -1;
    }

    *direction = relative[parent_direction][child_direction];
    irq_restore(irq);
    return 0;
}

int vmu_lcd_is_ready(const maple_device_t *dev) {
    irq_mask_t irq;
    int ready;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !dev->status ||
       !(dev->info.functions & MAPLE_FUNC_LCD)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    if(!vmu_has_standard_lcd(dev)) {
        irq_restore(irq);
        errno = ENOTSUP;
        return -1;
    }

    ready = dev->frame.state == MAPLE_FRAME_VACANT;
    irq_restore(irq);
    return ready;
}

int vmu_lcd_get_status(const maple_device_t *dev, vmu_lcd_status_t *status) {
    const vmu_driver_status_t *driver_status;
    irq_mask_t irq;

    if(!dev || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !dev->status ||
       !(dev->info.functions & MAPLE_FUNC_LCD)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    driver_status = dev->status;
    *status = driver_status->lcd;
    irq_restore(irq);
    return 0;
}

int vmu_lcd_set_completion_handler(maple_device_t *dev,
                                   vmu_lcd_completion_handler_t handler,
                                   void *user_data) {
    vmu_driver_status_t *status;
    irq_mask_t irq;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !dev->status ||
       !(dev->info.functions & MAPLE_FUNC_LCD)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = dev->status;
    status->lcd_handler = handler;
    status->lcd_handler_data = user_data;
    irq_restore(irq);
    return 0;
}

int vmu_lcd_pack_grayscale(void *bitmap, const uint8_t *pixels,
                           vmu_lcd_flip_t flip) {
    uint8_t *output = bitmap;
    unsigned int x;
    unsigned int y;

    if(!bitmap || !pixels ||
       (flip & ~(VMU_LCD_FLIP_HORIZONTAL | VMU_LCD_FLIP_VERTICAL))) {
        errno = EINVAL;
        return -1;
    }

    memset(output, 0, VMU_SCREEN_BITMAP_BYTES);

    for(y = 0; y < VMU_SCREEN_HEIGHT; ++y) {
        const unsigned int logical_y =
            (flip & VMU_LCD_FLIP_VERTICAL) ?
            VMU_SCREEN_HEIGHT - 1 - y : y;

        for(x = 0; x < VMU_SCREEN_WIDTH; ++x) {
            unsigned int logical_x;
            unsigned int output_offset;

            if(!(pixels[y * VMU_SCREEN_WIDTH + x] & 0x08))
                continue;

            logical_x = (flip & VMU_LCD_FLIP_HORIZONTAL) ?
                        VMU_SCREEN_WIDTH - 1 - x : x;

            /* vmu_draw_lcd() consumes the peripheral's bottom-right-first
               bit order. Reverse both logical axes and the bit position so
               the byte-per-pixel input remains conventional top-left-first. */
            output_offset = (VMU_SCREEN_HEIGHT - 1 - logical_y) *
                            (VMU_SCREEN_WIDTH / 8) +
                            (VMU_SCREEN_WIDTH - 1 - logical_x) / 8;
            output[output_offset] |= (uint8_t)(1u << (logical_x & 7));
        }
    }

    return 0;
}

static void vmu_lcd_reply(maple_state_t *state, maple_frame_t *frame) {
    maple_response_t *response = (maple_response_t *)frame->recv_buf;
    vmu_driver_status_t *status = frame->dev ? frame->dev->status : NULL;
    vmu_lcd_completion_handler_t handler = NULL;
    void *handler_data = NULL;
    uint32_t sequence = 0;
    int result = MAPLE_EFAIL;

    (void)state;

    if(status) {
        result = response->response == MAPLE_RESPONSE_OK ?
                 MAPLE_EOK : MAPLE_EFAIL;
        status->lcd.busy = false;
        status->lcd.result = result;
        status->lcd.response = response->response;
        status->lcd.completed_sequence = status->lcd.submitted_sequence;
        sequence = status->lcd.completed_sequence;
        handler = status->lcd_handler;
        handler_data = status->lcd_handler_data;
    }

    /* Release the shared device frame before the optional bounded callback so
       it may submit the next display without observing a false busy state. */
    maple_frame_unlock(frame);

    if(handler && frame->dev)
        handler(frame->dev, result, response->response, sequence,
                handler_data);
}

/* Draw a 1-bit bitmap on a standard 48x32 LCD. Submission is asynchronous;
   vmu_lcd_get_status() publishes the eventual peripheral response. */
int vmu_draw_lcd(maple_device_t *dev, const void *bitmap) {
    vmu_driver_status_t *status;
    irq_mask_t irq;

    if(!dev || !bitmap || !dev->valid || !dev->status ||
       !vmu_has_standard_lcd(dev))
        return MAPLE_EINVALID;

    /* Lock the frame */
    if(maple_frame_trylock(&dev->frame) < 0)
        return MAPLE_EAGAIN;

    status = dev->status;

    /* Reset the frame */
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_LCD;
    dev->frame.send_buf[1] = 0;    /* Block / phase / partition */
    memcpy(dev->frame.send_buf + 2, bitmap, VMU_SCREEN_WIDTH * 4);
    dev->frame.cmd = MAPLE_COMMAND_BWRITE;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2 + VMU_SCREEN_WIDTH;
    dev->frame.callback = vmu_lcd_reply;

    irq = irq_disable();
    status->lcd.busy = true;
    status->lcd.result = MAPLE_EAGAIN;
    status->lcd.response = MAPLE_RESPONSE_NONE;
    status->lcd.submitted_sequence =
        next_lcd_sequence(status->lcd.submitted_sequence);
    irq_restore(irq);

    if(maple_queue_frame(&dev->frame) < 0) {
        irq = irq_disable();
        status->lcd.busy = false;
        status->lcd.result = MAPLE_EFAIL;
        status->lcd.completed_sequence = status->lcd.submitted_sequence;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        irq_restore(irq);
        return MAPLE_EFAIL;
    }

    return MAPLE_EOK;
}

int vmu_draw_lcd_grayscale(maple_device_t *dev, const uint8_t *pixels,
                           vmu_lcd_flip_t flip) {
    uint8_t bitmap[VMU_SCREEN_BITMAP_BYTES];

    if(vmu_lcd_pack_grayscale(bitmap, pixels, flip) < 0)
        return MAPLE_EINVALID;

    return vmu_draw_lcd(dev, bitmap);
}

int vmu_draw_lcd_rotated(maple_device_t *dev, const void *bitmap) {
    uint32_t bitmap_inverted[48];
    unsigned int i;

    if(!bitmap)
        return MAPLE_EINVALID;

    for(i = 0; i < 48; i++) {
        uint32_t source;

        /* memcpy keeps the packed-input API valid for unaligned buffers. */
        memcpy(&source, (const uint8_t *)bitmap + (47 - i) * 4,
               sizeof(source));
        bitmap_inverted[i] = bit_reverse(source);
    }

    return vmu_draw_lcd(dev, bitmap_inverted);
}

/* This function converts a xbm image to a 1-bit bitmap that can
   be displayed on LCD screen of VMU */
static void vmu_xbm_to_bitmap(uint8_t *bitmap, const char *vmu_icon) {
    int x, y, xi, xb;
    memset(bitmap, 0, VMU_SCREEN_WIDTH * VMU_SCREEN_HEIGHT / 8);

    if(vmu_icon) {
        for(y = 0; y < VMU_SCREEN_HEIGHT; y++)
            for(x = 0; x < VMU_SCREEN_WIDTH; x++) {
                xi = x / 8;
                xb = 0x80 >> (x % 8);

                if(vmu_icon[((VMU_SCREEN_HEIGHT - 1) - y) * VMU_SCREEN_WIDTH 
                          + ((VMU_SCREEN_WIDTH  - 1) - x)] == '.')
                    bitmap[y * (VMU_SCREEN_WIDTH / 8) + xi] |= xb;
            }
    }
}

int vmu_draw_lcd_xbm(maple_device_t *dev, const char *vmu_icon) {
    uint8_t  bitmap[VMU_SCREEN_WIDTH * VMU_SCREEN_HEIGHT / 8];
    vmu_xbm_to_bitmap(bitmap, vmu_icon);

    return vmu_draw_lcd(dev, bitmap);
}

/* Utility function which sets the icon on all available VMUs
   from an Xwindows XBM. Imported from libdcutils. */
void vmu_set_icon(const char *vmu_icon) {
    int            i = 0;
    maple_device_t *dev;
    uint8_t        bitmap[VMU_SCREEN_WIDTH * VMU_SCREEN_HEIGHT / 8];

    vmu_xbm_to_bitmap(bitmap, vmu_icon);

    while((dev = maple_enum_type(i++, MAPLE_FUNC_LCD))) {
        vmu_draw_lcd(dev, bitmap);
    }
}

/* Read the data in block blocknum into buffer, return a -1
   if an error occurs, for now we ignore MAPLE_RESPONSE_FILEERR,
   which will be changed shortly */
static void vmu_block_read_callback(maple_state_t *st, maple_frame_t *frm) {
    (void)st;

    /* Wakey, wakey! */
    genwait_wake_all(frm);
}

int vmu_block_read(maple_device_t *dev, uint16_t blocknum, uint8_t *buffer) {
    maple_response_t *resp;
    int              rv;
    uint32_t         blkid, *send_buf;

    assert(dev != NULL);

    /* Lock the frame */
    maple_frame_lock(&dev->frame);

    /* This is (block << 24) | (phase << 8) | (partition (0 for all vmu)) */
    blkid = ((blocknum & 0xff) << 24) | ((blocknum >> 8) << 16);

    /* Reset the frame */
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_MEMCARD;
    dev->frame.send_buf[1] = blkid;
    dev->frame.cmd = MAPLE_COMMAND_BREAD;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = vmu_block_read_callback;
    maple_queue_frame(&dev->frame);

    /* Wait for the VMU to accept it */
    if(genwait_wait(&dev->frame, "vmu_block_read", 100) < 0) {
        if(dev->frame.state != MAPLE_FRAME_RESPONDED) {
            /* It's probably never coming back, so just unlock the frame */
            dev->frame.state = MAPLE_FRAME_VACANT;
            dbglog(DBG_ERROR, "vmu_block_read: timeout to unit %c%c, block %d\n",
                   dev->port + 'A', dev->unit + '0', (int)blocknum);
            return MAPLE_ETIMEOUT;
        }
    }

    if(dev->frame.state != MAPLE_FRAME_RESPONDED) {
        dbglog(DBG_ERROR, "vmu_block_read: incorrect state for unit %c%c, block %d (%d)\n",
               dev->port + 'A', dev->unit + '0', (int)blocknum, dev->frame.state);
        dev->frame.state = MAPLE_FRAME_VACANT;
        return MAPLE_EFAIL;
    }

    /* Copy out the response */
    resp = (maple_response_t *)dev->frame.recv_buf;
    send_buf = (uint32_t *)resp->data;

    if(resp->response != MAPLE_RESPONSE_DATATRF
            || send_buf[0] != MAPLE_FUNC_MEMCARD
            || send_buf[1] != blkid) {
        rv = MAPLE_EFAIL;
        dbglog(DBG_ERROR, "vmu_block_read failed: %s(%d)/%08lx\r\n",
               maple_perror(resp->response), resp->response, send_buf[0]);
    }
    else {
        rv = MAPLE_EOK;
        memcpy(buffer, send_buf + 2, (resp->data_len - 2) * 4);
    }

    maple_frame_unlock(&dev->frame);

    return rv;
}

/* writes buffer into block blocknum.  ret a -1 on error.  We don't do anything about the
   maple bus returning file errors, etc, right now, but that will change soon. */
static void vmu_block_write_callback(maple_state_t *st, maple_frame_t *frm) {
    (void)st;

    /* Reset the frame status (but still keep it for us to use) */
    frm->state = MAPLE_FRAME_UNSENT;

    /* Wakey, wakey! */
    genwait_wake_all(frm);
}

static int vmu_block_write_internal(maple_device_t *dev, uint16_t blocknum, const uint8_t *buffer) {
    maple_response_t *resp;
    int              rv, phase, r;
    uint32_t         blkid;

    assert(dev != NULL);

    /* Assume success */
    rv = MAPLE_EOK;

    /* Lock the frame. XXX: Priority inversion issues here. */
    maple_frame_lock(&dev->frame);

    /* Writes have to occur in four phases per block -- this is the
       way of flash memory, which you must erase an entire block
       at once to write; the blocks in this case are 128 bytes long. */
    for(phase = 0; phase < 4; phase++) {
        /* this is (block << 24) | (phase << 8) | (partition (0 for all vmu)) */
        blkid = ((blocknum & 0xff) << 24) | ((blocknum >> 8) << 16) | (phase << 8);

        /* Reset the frame */
        maple_frame_init(&dev->frame);
        dev->frame.send_buf[0] = MAPLE_FUNC_MEMCARD;
        dev->frame.send_buf[1] = blkid;
        memcpy(dev->frame.send_buf + 2, buffer + 128 * phase, 128);
        dev->frame.cmd = MAPLE_COMMAND_BWRITE;
        dev->frame.dst_port = dev->port;
        dev->frame.dst_unit = dev->unit;
        dev->frame.length = 2 + (128 / 4);
        dev->frame.callback = vmu_block_write_callback;
        maple_queue_frame(&dev->frame);

        /* Wait for the VMU to accept it */
        if(genwait_wait(&dev->frame, "vmu_block_write", 100) < 0) {
            if(dev->frame.state != MAPLE_FRAME_UNSENT) {
                /* It's probably never coming back, so just unlock the frame */
                dev->frame.state = MAPLE_FRAME_VACANT;
                dbglog(DBG_ERROR, "vmu_block_write: timeout to unit %c%c, block %d\n",
                       dev->port + 'A', dev->unit + '0', (int)blocknum);
                return MAPLE_ETIMEOUT;
            }
        }

        if(dev->frame.state != MAPLE_FRAME_UNSENT) {
            dbglog(DBG_ERROR, "vmu_block_read: incorrect state for unit %c%c, block %d (%d)\n",
                   dev->port + 'A', dev->unit + '0', (int)blocknum, dev->frame.state);
            dev->frame.state = MAPLE_FRAME_VACANT;
            return MAPLE_EFAIL;
        }

        /* Check the response */
        resp = (maple_response_t *)dev->frame.recv_buf;
        r = resp->response;

        if(r != MAPLE_RESPONSE_OK) {
            rv = MAPLE_EFAIL;
            dbglog(DBG_ERROR, "Incorrect response writing phase %d:\n", phase);
            dbglog(DBG_ERROR, "response:      %s(%d)\n", maple_perror(resp->response), resp->response);
            dbglog(DBG_ERROR, "datalen:       %d\n", resp->data_len);
        }
    }

    /* Finally a "sync" command -- thanks Nagra */
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_MEMCARD;
    dev->frame.send_buf[1] = ((blocknum & 0xff) << 24)
                  | (((blocknum >> 8) & 0xff) << 16)
                  | (4 << 8);
    dev->frame.cmd = MAPLE_COMMAND_BSYNC;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = vmu_block_write_callback;
    maple_queue_frame(&dev->frame);

    /* Wait for the VMU to accept it */
    if(genwait_wait(&dev->frame, "vmu_block_write", 100) < 0) {
        if(dev->frame.state != MAPLE_FRAME_UNSENT) {
            /* It's probably never coming back, so just unlock the frame */
            dev->frame.state = MAPLE_FRAME_VACANT;
            dbglog(DBG_ERROR, "vmu_block_write: timeout to unit %c%c, block %d\n",
                   dev->port + 'A', dev->unit + '0', (int)blocknum);
            return MAPLE_ETIMEOUT;
        }
    }

    if(dev->frame.state != MAPLE_FRAME_UNSENT) {
        dbglog(DBG_ERROR, "vmu_block_read: incorrect state for unit %c%c, block %d (%d)\n",
               dev->port + 'A', dev->unit + '0', (int)blocknum, dev->frame.state);
        dev->frame.state = MAPLE_FRAME_VACANT;
        return MAPLE_EFAIL;
    }

    dev->frame.state = MAPLE_FRAME_VACANT;

    return rv;
}

/* Sometimes a flaky or stubborn card can be recovered by trying a couple
   of times... */
int vmu_block_write(maple_device_t *dev, uint16_t blocknum, const uint8_t *buffer) {
    int i, rv;

    for(i = 0; i < 4; i++) {
        // Try the write.
        rv = vmu_block_write_internal(dev, blocknum, buffer);

        if(rv == MAPLE_EOK)
            return rv;

        /* It failed -- wait a bit and try again. */
        thd_sleep(VMU_BLOCK_WRITE_RETRY_TIME);
    }

    /* Well, looks like it's really toasty... return the most recent
       error. */
    return rv;
}

static int vmu_clock_decode_response(const maple_response_t *response,
                                     vmu_clock_operation_t operation,
                                     vmu_clock_time_t *time) {
    const uint32_t *words = (const uint32_t *)response->data;
    vmu_datetime_t wire;

    if(operation == VMU_CLOCK_OPERATION_SET)
        return response->response == MAPLE_RESPONSE_OK ?
               MAPLE_EOK : MAPLE_EFAIL;

    if(operation != VMU_CLOCK_OPERATION_GET || !time ||
       response->response != MAPLE_RESPONSE_DATATRF ||
       response->data_len < 3 || words[0] != MAPLE_FUNC_CLOCK)
        return MAPLE_EFAIL;

    memcpy(&wire, words + 1, sizeof(wire));
    vmu_datetime_to_clock_time(time, &wire);
    if(!vmu_clock_time_fields_valid(time, false))
        return MAPLE_EFAIL;
    time->weekday = vmu_clock_calculate_weekday(time->year, time->month,
                                                time->day);
    return MAPLE_EOK;
}

int vmu_clock_is_ready(const maple_device_t *dev) {
    irq_mask_t irq;
    int ready;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    if(!vmu_has_clock(dev) || !dev->status) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    ready = dev->frame.state == MAPLE_FRAME_VACANT;
    irq_restore(irq);
    return ready;
}

int vmu_clock_get_status(const maple_device_t *dev,
                         vmu_clock_status_t *clock_status) {
    const vmu_driver_status_t *status;
    irq_mask_t irq;

    if(!dev || !clock_status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    if(!vmu_has_clock(dev) || !dev->status) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = dev->status;
    *clock_status = status->clock;
    irq_restore(irq);
    return 0;
}

int vmu_clock_set_completion_handler(
    maple_device_t *dev, vmu_clock_completion_handler_t handler,
    void *user_data) {
    vmu_driver_status_t *status;
    irq_mask_t irq;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    if(!vmu_has_clock(dev) || !dev->status) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = dev->status;
    status->clock_handler = handler;
    status->clock_handler_data = user_data;
    irq_restore(irq);
    return 0;
}

static void vmu_clock_async_reply(maple_state_t *state,
                                  maple_frame_t *frame) {
    maple_response_t *response = (maple_response_t *)frame->recv_buf;
    maple_device_t *dev = frame->dev;
    vmu_driver_status_t *status = dev ? dev->status : NULL;
    vmu_clock_completion_handler_t handler = NULL;
    void *handler_data = NULL;
    vmu_clock_status_t completion = { 0 };
    int result = MAPLE_EFAIL;

    (void)state;

    if(status) {
        result = vmu_clock_decode_response(response, status->clock.operation,
                                           &status->clock.time);
        status->clock.busy = false;
        status->clock.time_valid = result == MAPLE_EOK;
        status->clock.result = result;
        status->clock.response = response->response;
        status->clock.completed_sequence =
            status->clock.submitted_sequence;
        completion = status->clock;
        handler = status->clock_handler;
        handler_data = status->clock_handler_data;
    }

    /* Make a chained clock or unrelated VMU command admissible to callbacks. */
    maple_frame_unlock(frame);

    if(handler && dev)
        handler(dev, &completion, handler_data);
}

static int vmu_clock_submit_async(maple_device_t *dev,
                                  vmu_clock_operation_t operation,
                                  const vmu_clock_time_t *time) {
    vmu_driver_status_t *status;
    vmu_datetime_t wire;
    irq_mask_t irq;

    if(!vmu_has_clock(dev) || !dev->status ||
       (operation == VMU_CLOCK_OPERATION_SET &&
        !vmu_clock_time_fields_valid(time, true)))
        return MAPLE_EINVALID;

    if(maple_frame_trylock(&dev->frame) < 0)
        return MAPLE_EAGAIN;

    status = dev->status;
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CLOCK;
    dev->frame.send_buf[1] = 0;
    dev->frame.cmd = operation == VMU_CLOCK_OPERATION_GET ?
                     MAPLE_COMMAND_BREAD : MAPLE_COMMAND_BWRITE;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = operation == VMU_CLOCK_OPERATION_GET ? 2 : 4;
    dev->frame.callback = vmu_clock_async_reply;

    if(operation == VMU_CLOCK_OPERATION_SET) {
        vmu_datetime_from_clock_time(&wire, time);
        memcpy(dev->frame.send_buf + 2, &wire, sizeof(wire));
    }

    irq = irq_disable();
    status->clock.busy = true;
    status->clock.time_valid = false;
    status->clock.operation = operation;
    status->clock.result = MAPLE_EAGAIN;
    status->clock.response = MAPLE_RESPONSE_NONE;
    status->clock.submitted_sequence =
        next_clock_sequence(status->clock.submitted_sequence);
    if(operation == VMU_CLOCK_OPERATION_SET)
        status->clock.time = *time;
    irq_restore(irq);

    if(maple_queue_frame(&dev->frame) < 0) {
        irq = irq_disable();
        status->clock.busy = false;
        status->clock.result = MAPLE_EFAIL;
        status->clock.completed_sequence =
            status->clock.submitted_sequence;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        irq_restore(irq);
        return MAPLE_EFAIL;
    }

    return MAPLE_EOK;
}

int vmu_clock_get_time_async(maple_device_t *dev) {
    return vmu_clock_submit_async(dev, VMU_CLOCK_OPERATION_GET, NULL);
}

int vmu_clock_set_time_async(maple_device_t *dev,
                             const vmu_clock_time_t *time) {
    return vmu_clock_submit_async(dev, VMU_CLOCK_OPERATION_SET, time);
}

static void vmu_clock_sync_reply(maple_state_t *state, maple_frame_t *frame) {
    maple_device_t *dev = frame->dev;
    vmu_driver_status_t *status = dev ? dev->status : NULL;

    (void)state;

    /* A live waiter owns the response buffer until it has decoded it. If the
       waiter already timed out, this late completion owns final cleanup. */
    if(status && status->clock_sync_waiting)
        genwait_wake_all(frame);
    else
        maple_frame_unlock(frame);
}

static int vmu_clock_wait_for_response(maple_device_t *dev,
                                       const char *wait_message,
                                       uint32_t timeout) {
    vmu_driver_status_t *status = dev->status;
    irq_mask_t irq;
    int frame_state;
    int wait_result;

    wait_result = genwait_wait(&dev->frame, wait_message, timeout);
    irq = irq_disable();
    frame_state = dev->frame.state;

    /* Recheck terminal state while the completion IRQ is fenced. A response
       may arrive after the timed wait expires but before this reconciliation. */
    if(frame_state == MAPLE_FRAME_RESPONDED) {
        status->clock_sync_waiting = false;
        irq_restore(irq);
        return MAPLE_EOK;
    }

    status->clock_sync_waiting = false;

    /* An unsent frame can be withdrawn. Once submitted, retain ownership until
       the completion path consumes its DMA response and releases the frame. */
    if(frame_state == MAPLE_FRAME_UNSENT && dev->frame.queued) {
        maple_queue_remove(&dev->frame);
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
    }

    irq_restore(irq);

    if(wait_result < 0) {
        dbglog(DBG_ERROR, "%s: timeout to unit %c%c\n", wait_message,
               dev->port + 'A', dev->unit + '0');
        return MAPLE_ETIMEOUT;
    }

    dbglog(DBG_ERROR, "%s: incorrect state for unit %c%c (%d)\n",
           wait_message, dev->port + 'A', dev->unit + '0', frame_state);
    return MAPLE_EFAIL;
}

int vmu_clock_get_time(maple_device_t *dev, vmu_clock_time_t *time) {
    vmu_driver_status_t *status;
    maple_response_t *response;
    int result;

    if(!time || !vmu_has_clock(dev) || !dev->status)
        return MAPLE_EINVALID;

    status = dev->status;
    maple_frame_lock(&dev->frame);
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CLOCK;
    dev->frame.send_buf[1] = 0;
    dev->frame.cmd = MAPLE_COMMAND_BREAD;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = vmu_clock_sync_reply;
    status->clock_sync_waiting = true;

    if(maple_queue_frame(&dev->frame) < 0) {
        status->clock_sync_waiting = false;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        return MAPLE_EFAIL;
    }

    result = vmu_clock_wait_for_response(dev, "vmu_clock_get_time", 10000);
    if(result != MAPLE_EOK)
        return result;

    response = (maple_response_t *)dev->frame.recv_buf;
    result = vmu_clock_decode_response(response, VMU_CLOCK_OPERATION_GET,
                                       time);
    status->clock_sync_waiting = false;
    maple_frame_unlock(&dev->frame);
    return result;
}

int vmu_clock_set_time(maple_device_t *dev,
                       const vmu_clock_time_t *time) {
    vmu_driver_status_t *status;
    maple_response_t *response;
    vmu_datetime_t wire;
    int result;

    if(!vmu_has_clock(dev) || !dev->status ||
       !vmu_clock_time_fields_valid(time, true))
        return MAPLE_EINVALID;

    status = dev->status;
    maple_frame_lock(&dev->frame);
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CLOCK;
    dev->frame.send_buf[1] = 0;
    vmu_datetime_from_clock_time(&wire, time);
    memcpy(dev->frame.send_buf + 2, &wire, sizeof(wire));
    dev->frame.cmd = MAPLE_COMMAND_BWRITE;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 4;
    dev->frame.callback = vmu_clock_sync_reply;
    status->clock_sync_waiting = true;

    if(maple_queue_frame(&dev->frame) < 0) {
        status->clock_sync_waiting = false;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        return MAPLE_EFAIL;
    }

    result = vmu_clock_wait_for_response(dev, "vmu_clock_set_time", 500);
    if(result != MAPLE_EOK)
        return result;

    response = (maple_response_t *)dev->frame.recv_buf;
    result = vmu_clock_decode_response(response, VMU_CLOCK_OPERATION_SET,
                                       NULL);
    status->clock_sync_waiting = false;
    maple_frame_unlock(&dev->frame);
    return result;
}

int vmu_set_datetime(maple_device_t *dev, time_t unix_time) {
    const struct tm *broken_down = localtime(&unix_time);
    vmu_clock_time_t time;

    if(!broken_down)
        return MAPLE_EINVALID;

    vmu_datetime_from_tm(&time, broken_down);
    return vmu_clock_set_time(dev, &time);
}

int vmu_get_datetime(maple_device_t *dev, time_t *unix_time) {
    vmu_clock_time_t time;
    struct tm broken_down = { 0 };
    int result;

    if(!unix_time)
        return MAPLE_EINVALID;

    *unix_time = (time_t)-1;
    result = vmu_clock_get_time(dev, &time);
    if(result != MAPLE_EOK)
        return result;

    vmu_datetime_to_tm(&time, &broken_down);
    *unix_time = mktime(&broken_down);
    return *unix_time == (time_t)-1 ? MAPLE_EFAIL : MAPLE_EOK;
}
