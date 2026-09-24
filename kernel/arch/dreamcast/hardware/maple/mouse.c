/* KallistiOS ##version##

   mouse.c
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

#include <dc/maple.h>
#include <dc/maple/mouse.h>
#include <kos/irq.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

/* Mouse center value in the raw condition structure. */
#define MOUSE_DELTA_CENTER      0x200

/* The Maple pointing-device condition is fixed at five words after its
   function word: two button/flag bytes followed by eight centered axes. */
typedef struct mouse_cond {
    uint8_t buttons;
    uint8_t options;
    uint8_t overflow;
    uint8_t reserved;
    uint16_t axis[8];
} mouse_cond_t;

_Static_assert(sizeof(mouse_cond_t) == 20,
               "Maple mouse condition must occupy five words");

/* mouse_state_t remains first so legacy maple_dev_status() casts retain the
   same initial field offsets while the coherent API adds transitions. */
typedef struct mouse_status {
    mouse_state_t state;
    uint32_t pressed;
    uint32_t released;
    uint32_t sequence;
} mouse_status_t;

static int decode_axis(uint16_t raw) {
    return (int)raw - MOUSE_DELTA_CENTER;
}

int mouse_get_snapshot(const maple_device_t *device,
                       mouse_snapshot_t *snapshot) {
    const mouse_status_t *status;
    irq_mask_t irq;

    if(!device || !snapshot) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!device->valid || !(device->info.functions & MAPLE_FUNC_MOUSE) ||
       !device->status) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = (const mouse_status_t *)device->status;

    if(!status->sequence) {
        irq_restore(irq);
        errno = EAGAIN;
        return -1;
    }

    snapshot->state = status->state;
    snapshot->pressed = status->pressed;
    snapshot->released = status->released;
    snapshot->sequence = status->sequence;
    irq_restore(irq);
    return 0;
}

int mouse_get_info(const maple_device_t *device, mouse_info_t *info) {
    uint32_t descriptor;
    irq_mask_t irq;

    if(!device || !info) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!device->valid || !(device->info.functions & MAPLE_FUNC_MOUSE) ||
       !maple_dev_function_data(device, MAPLE_FUNC_MOUSE, &descriptor)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    /* Byte-oriented Maple descriptors appear least-significant byte first in
       the stored word order returned by maple_dev_function_data(). */
    info->reserved = descriptor & 0xff;
    info->buttons = (descriptor >> 8) & 0xff;
    info->axes = (descriptor >> 16) & 0xff;
    info->reserved2 = (descriptor >> 24) & 0xff;
    irq_restore(irq);
    return 0;
}

static void mouse_reply(maple_state_t *st, maple_frame_t *frm) {
    (void)st;

    maple_response_t    *resp;
    uint32_t            *respbuf;
    mouse_cond_t        *raw;
    mouse_status_t      *status;
    mouse_state_t       *cooked;
    uint32_t             old_buttons;
    size_t               i;

    /* Unlock the frame now (it's ok, we're in an IRQ) */
    maple_frame_unlock(frm);

    /* Make sure we got a valid response */
    resp = (maple_response_t *)frm->recv_buf;

    if(resp->response != MAPLE_RESPONSE_DATATRF)
        return;

    respbuf = (uint32_t *)resp->data;

    if(respbuf[0] != MAPLE_FUNC_MOUSE)
        return;

    if(!frm->dev)
        return;

    /* Malformed or truncated third-party responses are ignored in IRQ
       context rather than asserting or reading beyond the DMA response. */
    if(resp->data_len != 1 + sizeof(mouse_cond_t) / sizeof(uint32_t))
        return;

    raw = (mouse_cond_t *)(respbuf + 1);

    status = (mouse_status_t *)frm->dev->status;
    cooked = &status->state;
    old_buttons = cooked->buttons;

    /* Mouse buttons are active-low on the wire. Retain all eight standardized
       pointing-device buttons; the previous driver silently dropped middle
       and auxiliary buttons by masking the result to bits 1 through 3. */
    cooked->buttons = (~raw->buttons) & 0xff;
    cooked->dx = decode_axis(raw->axis[0]);
    cooked->dy = decode_axis(raw->axis[1]);
    cooked->dz = decode_axis(raw->axis[2]);

    for(i = 0; i < 5; ++i)
        cooked->axis[i] = decode_axis(raw->axis[i + 3]);

    cooked->overflow = raw->overflow;
    cooked->options = raw->options;
    memset(cooked->reserved, 0, sizeof(cooked->reserved));

    status->pressed = cooked->buttons & ~old_buttons;
    status->released = old_buttons & ~cooked->buttons;

    if(++status->sequence == 0)
        status->sequence = 1;
}

static int mouse_poll(maple_device_t *dev) {
    if(maple_frame_trylock(&dev->frame) < 0)
        return 0;

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_MOUSE;
    dev->frame.cmd = MAPLE_COMMAND_GETCOND;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 1;
    dev->frame.callback = mouse_reply;
    maple_queue_frame(&dev->frame);

    return 0;
}

static void mouse_periodic(maple_driver_t *drv) {
    maple_driver_foreach(drv, mouse_poll);
}

/* Device Driver Struct */
static maple_driver_t mouse_drv = {
    .functions = MAPLE_FUNC_MOUSE,
    .name = "Mouse Driver",
    .periodic = mouse_periodic,
    .status_size = sizeof(mouse_status_t)
};

/* Add the mouse to the driver chain */
void mouse_init(void) {
    maple_driver_reg(&mouse_drv);
}

void mouse_shutdown(void) {
    maple_driver_unreg(&mouse_drv);
}
