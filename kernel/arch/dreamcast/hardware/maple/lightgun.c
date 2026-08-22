/* KallistiOS ##version##

   lightgun.c
   Copyright (C) 2015 Lawrence Sebald
   Copyright (C) 2026 KallistiOS Contributors
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/lightgun.h>
#include <dc/pvr.h>
#include <kos/irq.h>

static uint8_t enabled_ports;
static uint8_t capture_requests;
static bool capture_in_progress;
/* Match maple_state.vbl_cntr's type so wraparound comparisons do not promote
   the signed counter to unsigned and trigger misleading diagnostics. */
static int capture_vblank;

static uint32_t flash_color;
static uint32_t saved_border_color;
static uint32_t saved_video_cfg_bit;
static bool flash_active;

static lightgun_snapshot_t latest_capture;
static lightgun_capture_handler_t capture_handler;
static void *capture_handler_data;
static lightgun_trigger_filter_t trigger_filter;
static void *trigger_filter_data;

static void restore_capture_flash(void) {
    uint32_t video_cfg;

    if(!flash_active)
        return;

    PVR_SET(PVR_BORDER_COLOR, saved_border_color);
    video_cfg = PVR_GET(PVR_VIDEO_CFG);
    video_cfg &= ~BIT(3);
    video_cfg |= saved_video_cfg_bit;
    PVR_SET(PVR_VIDEO_CFG, video_cfg);
    flash_active = false;
}

static void enable_capture_flash(void) {
    uint32_t video_cfg;

    if(!flash_color)
        return;

    saved_border_color = PVR_GET(PVR_BORDER_COLOR);
    video_cfg = PVR_GET(PVR_VIDEO_CFG);
    saved_video_cfg_bit = video_cfg & BIT(3);

    PVR_SET(PVR_BORDER_COLOR, flash_color & 0x00ffffffu);
    PVR_SET(PVR_VIDEO_CFG, video_cfg | BIT(3));
    flash_active = true;
}

static bool port_has_lightgun(int port) {
    const maple_device_t *dev = maple_enum_dev(port, 0);
    const uint32_t required = MAPLE_FUNC_CONTROLLER | MAPLE_FUNC_LIGHTGUN;

    return dev && dev->valid && (dev->info.functions & required) == required;
}

static void lightgun_periodic(maple_driver_t *drv) {
    uint8_t requests;
    int port;

    (void)drv;

    /* A hit can end Maple DMA before the video field ends. Keep the flash up
       until the next VBlank; a no-hit completion arriving at that VBlank can
       restore immediately from lightgun_capture_complete(). */
    if(flash_active && !capture_in_progress &&
       maple_state.vbl_cntr != capture_vblank)
        restore_capture_flash();

    /* Arming while another Maple transfer is still active would turn the flash
       on before the exclusive gun descriptor can actually be submitted. */
    if(capture_in_progress || maple_state.dma_in_progress ||
       maple_state.gun_port >= 0 || maple_state.gun_active_port >= 0)
        return;

    requests = capture_requests & enabled_ports;

    while(requests) {
        port = __builtin_ctz((unsigned int)requests);

        if(!port_has_lightgun(port)) {
            capture_requests &= (uint8_t)~BIT(port);
            requests &= (uint8_t)~BIT(port);
            continue;
        }

        if(maple_gun_enable(port) != MAPLE_EOK)
            return;

        capture_requests &= (uint8_t)~BIT(port);
        capture_in_progress = true;
        capture_vblank = maple_state.vbl_cntr;
        enable_capture_flash();
        return;
    }
}

int lightgun_set_enabled_ports(uint8_t port_mask) {
    irq_mask_t irq;

    if(port_mask & ~LIGHTGUN_PORT_ALL) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    enabled_ports = port_mask;
    capture_requests &= port_mask;
    irq_restore(irq);
    return 0;
}

uint8_t lightgun_get_enabled_ports(void) {
    irq_mask_t irq = irq_disable();
    uint8_t port_mask = enabled_ports;

    irq_restore(irq);
    return port_mask;
}

int lightgun_request_capture(int port) {
    irq_mask_t irq;

    if(port < 0 || port >= MAPLE_PORT_COUNT) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!(enabled_ports & BIT(port))) {
        irq_restore(irq);
        errno = EINVAL;
        return -1;
    }

    if(!port_has_lightgun(port)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    capture_requests |= BIT(port);
    irq_restore(irq);
    return 0;
}

int lightgun_get_snapshot(lightgun_snapshot_t *snapshot) {
    irq_mask_t irq;

    if(!snapshot) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!latest_capture.sequence) {
        irq_restore(irq);
        errno = EAGAIN;
        return -1;
    }

    *snapshot = latest_capture;
    irq_restore(irq);
    return 0;
}

void lightgun_set_capture_handler(lightgun_capture_handler_t callback,
                                  void *user_data) {
    irq_mask_t irq = irq_disable();

    capture_handler = callback;
    capture_handler_data = user_data;

    irq_restore(irq);
}

void lightgun_set_trigger_filter(lightgun_trigger_filter_t callback,
                                 void *user_data) {
    irq_mask_t irq = irq_disable();

    trigger_filter = callback;
    trigger_filter_data = user_data;

    irq_restore(irq);
}

void lightgun_set_flash_color(uint32_t rgb888) {
    irq_mask_t irq = irq_disable();

    flash_color = rgb888 & 0x00ffffffu;

    irq_restore(irq);
}

uint32_t lightgun_get_flash_color(void) {
    irq_mask_t irq = irq_disable();
    uint32_t color = flash_color;

    irq_restore(irq);
    return color;
}

void lightgun_controller_sample(maple_device_t *dev,
                                const cont_snapshot_t *snapshot) {
    const uint32_t required = MAPLE_FUNC_CONTROLLER | MAPLE_FUNC_LIGHTGUN;
    int allow_physical;

    if(!dev || !snapshot || dev->unit != 0 || dev->port < 0 ||
       dev->port >= MAPLE_PORT_COUNT ||
       (dev->info.functions & required) != required ||
       !(enabled_ports & BIT(dev->port)))
        return;

    allow_physical = !trigger_filter ||
                     trigger_filter(dev, snapshot, trigger_filter_data);

    if(allow_physical && (snapshot->pressed & CONT_A))
        capture_requests |= BIT(dev->port);
}

void lightgun_capture_complete(int port, int x, int y) {
    lightgun_snapshot_t snapshot;

    latest_capture.port = port;
    latest_capture.x = x;
    latest_capture.y = y;
    if(++latest_capture.sequence == 0)
        ++latest_capture.sequence;

    capture_in_progress = false;

    if(flash_active && maple_state.vbl_cntr != capture_vblank)
        restore_capture_flash();

    if(capture_handler) {
        snapshot = latest_capture;
        capture_handler(&snapshot, capture_handler_data);
    }
}

/* The official gun also advertises the controller function, so the controller
   driver owns that combined device and this periodic callback supplies the
   capture side independently. */
static maple_driver_t lightgun_drv = {
    .functions = MAPLE_FUNC_LIGHTGUN,
    .name = "Lightgun",
    .periodic = lightgun_periodic
};

void lightgun_init(void) {
    enabled_ports = 0;
    capture_requests = 0;
    capture_in_progress = false;
    capture_vblank = 0;
    flash_color = LIGHTGUN_FLASH_COLOR_DEFAULT;
    flash_active = false;
    memset(&latest_capture, 0, sizeof(latest_capture));
    capture_handler = NULL;
    capture_handler_data = NULL;
    trigger_filter = NULL;
    trigger_filter_data = NULL;
    maple_driver_reg(&lightgun_drv);
}

void lightgun_shutdown(void) {
    irq_mask_t irq = irq_disable();

    enabled_ports = 0;
    capture_requests = 0;
    capture_handler = NULL;
    capture_handler_data = NULL;
    trigger_filter = NULL;
    trigger_filter_data = NULL;
    maple_gun_disable();
    restore_capture_flash();

    irq_restore(irq);
    maple_driver_unreg(&lightgun_drv);
}
