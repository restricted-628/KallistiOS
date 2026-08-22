/* KallistiOS ##version##

   controller.c
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2024 Donald Haase
   Copyright (C) 2025 Falco Girgis
   Copyright (C) 2026 Joseph Black

 */

#include <arch/arch.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/lightgun.h>
#include <kos/mutex.h>
#include <kos/worker_thread.h>
#include <arch/irq.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

#ifndef CONT_BTN_CALLBACK_THD_STACK_SIZE
#define CONT_BTN_CALLBACK_THD_STACK_SIZE (8 * 1024)
#endif

/* Raw controller condition structure */
typedef struct cont_cond {
    uint16_t buttons;  /* buttons bitfield */
    uint8_t rtrig;     /* right trigger */
    uint8_t ltrig;     /* left trigger */
    uint8_t joyx;      /* joystick X */
    uint8_t joyy;      /* joystick Y */
    uint8_t joy2x;     /* second joystick X */
    uint8_t joy2y;     /* second joystick Y */
} cont_cond_t;

/* cont_state_t must remain first so maple_dev_status() stays compatible. */
typedef struct cont_status {
    cont_state_t state;
    uint32_t pressed;
    uint32_t released;
    uint32_t sequence;
} cont_status_t;

static struct {
    cont_sample_handler_t callback;
    void *user_data;
} sample_handler;

static uint8_t trigger_press_threshold = CONT_TRIGGER_PRESS_DEFAULT;
static uint8_t trigger_release_threshold = CONT_TRIGGER_RELEASE_DEFAULT;

typedef struct cont_callback_params {
    cont_btn_callback_t cb;
    uint8_t addr;
    uint32_t btns;
    kthread_worker_t *worker;

    uint8_t cur_addr;
    uint32_t cur_btns;

    TAILQ_ENTRY(cont_callback_params)  listent;
} cont_callback_params_t;

static TAILQ_HEAD(cont_btn_callback_list, cont_callback_params) btn_cbs;

static mutex_t btn_cbs_mtx = MUTEX_INITIALIZER;

/* Check whether the controller has EXACTLY the given capabilities. */
int __pure cont_is_type(const maple_device_t *cont, uint32_t type) {
    uint32_t capabilities;

    if(!cont || !cont->valid ||
       !maple_dev_function_data(cont, MAPLE_FUNC_CONTROLLER,
                                &capabilities))
        return -1;

    return capabilities == type;
}

/* Check whether the controller has at LEAST the given capabilities. */
int __pure cont_has_capabilities(const maple_device_t *cont, uint32_t capabilities) {
    uint32_t available;

    if(!cont || !cont->valid ||
       !maple_dev_function_data(cont, MAPLE_FUNC_CONTROLLER,
                                &available))
        return -1;

    return (available & capabilities) == capabilities;
}

int cont_get_snapshot(const maple_device_t *dev, cont_snapshot_t *snapshot) {
    const cont_status_t *status;
    irq_mask_t irq;

    if(!dev || !snapshot) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !(dev->info.functions & MAPLE_FUNC_CONTROLLER) ||
       !dev->status) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = (const cont_status_t *)dev->status;

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

void cont_set_sample_handler(cont_sample_handler_t callback, void *user_data) {
    irq_mask_t irq = irq_disable();

    sample_handler.callback = callback;
    sample_handler.user_data = user_data;

    irq_restore(irq);
}

int cont_set_trigger_thresholds(uint8_t press, uint8_t release) {
    irq_mask_t irq;

    if(press <= release) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();
    trigger_press_threshold = press;
    trigger_release_threshold = release;
    irq_restore(irq);
    return 0;
}

int cont_snapshot_is_soft_reset(const maple_device_t *dev,
                                const cont_snapshot_t *snapshot) {
    const uint32_t xy_capabilities = CONT_CAPABILITY_X | CONT_CAPABILITY_Y;
    uint32_t capabilities;
    uint32_t required = CONT_A | CONT_B;

    if(!dev || !snapshot || !(snapshot->pressed & CONT_START) ||
       !maple_dev_function_data(dev, MAPLE_FUNC_CONTROLLER, &capabilities))
        return 0;

    if((capabilities & xy_capabilities) == xy_capabilities)
        required |= CONT_X | CONT_Y;
    else if(capabilities & xy_capabilities)
        return 0;

    return (snapshot->state.buttons & required) == required;
}

/* This is an internal function for deleting a callback. It happens
    currently in two different ways. Either a NULL callback was
    requested for an addr/btns combination, or we are shutting down
    entirely by passing NULL and cleaning out all entries.

    XXX: This could be useful as part of a more robust Public API
    that would check the rest of the controller joy/trig/dpad and
    just have callback-based input handling.
*/
static void cont_btn_callback_del(cont_callback_params_t *params) {
    cont_callback_params_t *c, *n;

    mutex_lock_scoped(&btn_cbs_mtx);

    TAILQ_FOREACH_SAFE(c, &btn_cbs, listent, n) {
        if((params == NULL) || ((params->addr == c->addr) &&
            (params->btns == c->btns))) {

            if((params == NULL) || (params->cb == NULL) ||
                (params->cb == c->cb)) {
                TAILQ_REMOVE(&btn_cbs, c, listent);
                thd_worker_destroy(c->worker);
                free(c);
            }
        }
    }
}

static void cont_btn_cb_thread(void *d) {
    cont_callback_params_t *params = d;
    params->cb(params->addr, params->btns);
}

/* Set a controller callback for a button combo; set addr=0 for any controller */
int cont_btn_callback(uint8_t addr, uint32_t btns, cont_btn_callback_t cb) {
    cont_callback_params_t *params;

    params = (cont_callback_params_t *)malloc(sizeof(cont_callback_params_t));

    if(!params) return -1;

    params->addr = addr;
    params->btns = btns;
    params->cb = cb;

    /* This flags us to uninstall the handler for that addr/btn */
    if(cb == NULL) {
        cont_btn_callback_del(params);
        free(params);
        return 0;
    }

    const kthread_attr_t thd_attr = {
        .stack_size = CONT_BTN_CALLBACK_THD_STACK_SIZE,
        .prio = PRIO_DEFAULT,
        .label = "cont_btn_callback"
    };

    params->worker =
        thd_worker_create_ex(&thd_attr, &cont_btn_cb_thread, params);

    if(!params->worker) {
        free(params);
        return -1;
    }

    mutex_lock_scoped(&btn_cbs_mtx);

    if(addr)
        TAILQ_INSERT_HEAD(&btn_cbs, params, listent);
    else
        TAILQ_INSERT_TAIL(&btn_cbs, params, listent);

    return 0;
}

/* Response callback for the GETCOND Maple command. */
static void cont_reply(maple_state_t *st, maple_frame_t *frm) {
    (void)st;

    maple_response_t *resp;
    uint32_t         *respbuf;
    cont_cond_t      *raw;
    cont_status_t    *status;
    cont_state_t     *cooked;
    cont_snapshot_t   snapshot;
    uint32_t          old_buttons;
    cont_callback_params_t *c;

    /* Unlock the frame now (it's ok, we're in an IRQ) */
    maple_frame_unlock(frm);

    /* Make sure we got a valid response */
    resp = (maple_response_t *)frm->recv_buf;

    if(resp->response != MAPLE_RESPONSE_DATATRF)
        return;

    respbuf = (uint32_t *)resp->data;

    if(respbuf[0] != MAPLE_FUNC_CONTROLLER)
        return;

    if(!frm->dev)
        return;

    /* A malformed third-party response must be ignored, not turn into an
       assertion panic (or an unchecked short read when NDEBUG is enabled). */
    if(resp->data_len != 1 + sizeof(cont_cond_t) / sizeof(uint32_t))
        return;

    raw = (cont_cond_t *)(respbuf + 1);

    /* Fill the "nice" struct from the raw data */
    status = (cont_status_t *)frm->dev->status;
    cooked = &status->state;
    old_buttons = cooked->buttons;
    cooked->buttons = ((~raw->buttons) & 0xffff) |
                      (old_buttons & (CONT_RTRIG_DIGITAL |
                                      CONT_LTRIG_DIGITAL));
    cooked->ltrig = raw->ltrig;
    cooked->rtrig = raw->rtrig;
    cooked->joyx = ((int)raw->joyx) - 128;
    cooked->joyy = ((int)raw->joyy) - 128;
    cooked->joy2x = ((int)raw->joy2x) - 128;
    cooked->joy2y = ((int)raw->joy2y) - 128;

    if(raw->rtrig >= trigger_press_threshold)
        cooked->buttons |= CONT_RTRIG_DIGITAL;
    else if(raw->rtrig <= trigger_release_threshold)
        cooked->buttons &= ~CONT_RTRIG_DIGITAL;

    if(raw->ltrig >= trigger_press_threshold)
        cooked->buttons |= CONT_LTRIG_DIGITAL;
    else if(raw->ltrig <= trigger_release_threshold)
        cooked->buttons &= ~CONT_LTRIG_DIGITAL;

    status->pressed = cooked->buttons & ~old_buttons;
    status->released = old_buttons & ~cooked->buttons;
    if(++status->sequence == 0)
        ++status->sequence;

    snapshot.state = *cooked;
    snapshot.pressed = status->pressed;
    snapshot.released = status->released;
    snapshot.sequence = status->sequence;

    /* A light gun is a compound controller/light-gun Maple device. Let its
       capture scheduler consume the coherent trigger edge without taking over
       the controller driver's public sample callback. */
    lightgun_controller_sample(frm->dev, &snapshot);

    /* Deliver the newly decoded response without another frame of latency. */
    if(sample_handler.callback) {
        sample_handler.callback(frm->dev, &snapshot,
                                sample_handler.user_data);
    }

    /* If someone is in the middle of modifying the list, don't process callbacks */
    if(mutex_trylock(&btn_cbs_mtx))
        return;

    /* Check for magic button sequences */
    TAILQ_FOREACH(c, &btn_cbs, listent) {
        if(!c->addr ||
                (c->addr &&
                 c->addr == maple_addr(frm->dev->port, frm->dev->unit))) {
            if((cooked->buttons & c->btns) == c->btns) {
                c->cur_btns = cooked->buttons;
                c->cur_addr = maple_addr(frm->dev->port, frm->dev->unit);
                thd_worker_wakeup(c->worker);
            }
        }
    }

    mutex_unlock(&btn_cbs_mtx);
}

static int cont_poll(maple_device_t *dev) {
    if(maple_frame_trylock(&dev->frame) < 0)
        return 0;

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CONTROLLER;
    dev->frame.cmd = MAPLE_COMMAND_GETCOND;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 1;
    dev->frame.callback = cont_reply;
    maple_queue_frame(&dev->frame);

    return 0;
}

static void cont_periodic(maple_driver_t *drv) {
    maple_driver_foreach(drv, cont_poll);
}

/* Device Driver Struct */
static maple_driver_t controller_drv = {
    .functions = MAPLE_FUNC_CONTROLLER,
    .name = "Controller Driver",
    .periodic = cont_periodic,
    .status_size = sizeof(cont_status_t)
};

/* Add the controller to the driver chain */
void cont_init(void) {
    TAILQ_INIT(&btn_cbs);
    memset(&sample_handler, 0, sizeof(sample_handler));
    trigger_press_threshold = CONT_TRIGGER_PRESS_DEFAULT;
    trigger_release_threshold = CONT_TRIGGER_RELEASE_DEFAULT;
    maple_driver_reg(&controller_drv);
}

void cont_shutdown(void) {
    cont_set_sample_handler(NULL, NULL);
    /* Empty the callback list */
    cont_btn_callback_del(NULL);
    maple_driver_unreg(&controller_drv);
}
