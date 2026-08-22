/* KallistiOS ##version##

   purupuru.c
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2005 Lawrence Sebald
   Copyright (C) 2025 Donald Haase
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <dc/maple.h>
#include <dc/maple/purupuru.h>
#include <kos/genwait.h>
#include <kos/irq.h>

/* The public snapshot stays first so maple_dev_status() remains useful for
   callers that deliberately consume driver-owned status. Callback ownership
   and the timeout handoff for GETMINFO remain private to this driver. */
typedef struct purupuru_driver_status {
    purupuru_status_t public;
    purupuru_completion_handler_t completion_handler;
    void *completion_data;
} purupuru_driver_status_t;

typedef struct purupuru_query_context {
    volatile bool done;
    int response;
    uint8_t data_len;
    uint32_t function;
    uint32_t unit_info;
} purupuru_query_context_t;

/* One frame exists per device, so at most one query context can occupy a
   port/unit pair. IRQ exclusion orders publication, timeout withdrawal, and
   response copy-out without extending a stack pointer past the caller. */
static purupuru_query_context_t
    *active_queries[MAPLE_PORT_COUNT][MAPLE_UNIT_COUNT];

static bool purupuru_device_valid(const maple_device_t *dev) {
    return dev && dev->valid &&
           (dev->info.functions & MAPLE_FUNC_PURUPURU) && dev->status;
}

static uint32_t next_sequence(uint32_t sequence) {
    if(++sequence == 0)
        sequence = 1;

    return sequence;
}

int purupuru_effect_encode(const purupuru_effect_config_t *config,
                           purupuru_effect_t *effect) {
    uint8_t mode;
    uint8_t power;

    if(!config || !effect || config->unit == 0 ||
       config->unit > PURUPURU_MAX_UNITS || config->power < -7 ||
       config->power > 7 || config->ramp < PURUPURU_RAMP_NONE ||
       config->ramp > PURUPURU_RAMP_DOWN) {
        errno = EINVAL;
        return -1;
    }

    if((config->ramp == PURUPURU_RAMP_UP && config->power <= 0) ||
       (config->ramp == PURUPURU_RAMP_DOWN && config->power >= 0) ||
       (config->ramp == PURUPURU_RAMP_NONE && config->cycles != 0)) {
        errno = EINVAL;
        return -1;
    }

    mode = (uint8_t)(config->unit << 4);
    if(config->continuous)
        mode |= 0x01;

    if(config->power > 0)
        power = (uint8_t)config->power << 4;
    else if(config->power < 0)
        power = (uint8_t)-config->power;
    else
        power = 0;

    if(config->ramp == PURUPURU_RAMP_UP)
        power |= 0x08;
    else if(config->ramp == PURUPURU_RAMP_DOWN)
        power |= 0x80;

    effect->raw = mode | ((uint32_t)power << 8) |
                  ((uint32_t)config->frequency << 16) |
                  ((uint32_t)config->cycles << 24);
    return 0;
}

int purupuru_effect_decode(const purupuru_effect_t *effect,
                           purupuru_effect_config_t *config) {
    const uint8_t mode = effect ? effect->raw & 0xff : 0;
    const uint8_t power_byte = effect ? (effect->raw >> 8) & 0xff : 0;
    const uint8_t positive = (power_byte >> 4) & 0x07;
    const uint8_t negative = power_byte & 0x07;
    const bool ramp_up = power_byte & 0x08;
    const bool ramp_down = power_byte & 0x80;

    if(!effect || !config) {
        errno = EINVAL;
        return -1;
    }

    if(!(mode >> 4) || (mode & 0x0e) || (positive && negative) ||
       (ramp_up && ramp_down) || (ramp_up && !positive) ||
       (ramp_down && !negative)) {
        errno = EPROTO;
        return -1;
    }

    config->unit = mode >> 4;
    config->power = positive ? (int8_t)positive : -(int8_t)negative;
    config->frequency = (effect->raw >> 16) & 0xff;
    config->cycles = effect->raw >> 24;
    config->continuous = mode & 0x01;
    config->ramp = ramp_up ? PURUPURU_RAMP_UP :
                   (ramp_down ? PURUPURU_RAMP_DOWN : PURUPURU_RAMP_NONE);
    return 0;
}

int purupuru_get_info(const maple_device_t *dev, purupuru_info_t *info) {
    uint32_t descriptor;
    irq_mask_t irq;

    if(!dev || !info) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !(dev->info.functions & MAPLE_FUNC_PURUPURU) ||
       !maple_dev_function_data(dev, MAPLE_FUNC_PURUPURU, &descriptor)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    info->units = descriptor & 0xff;
    info->simultaneous_units = (descriptor >> 8) & 0xff;
    irq_restore(irq);

    if(!info->units || info->units > PURUPURU_MAX_UNITS ||
       !info->simultaneous_units ||
       info->simultaneous_units > info->units) {
        memset(info, 0, sizeof(*info));
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int purupuru_get_direction(const maple_device_t *dev,
                           purupuru_direction_t *direction) {
    static const purupuru_direction_t relative[4][4] = {
        { PURUPURU_DIRECTION_FLIPPED, PURUPURU_DIRECTION_NORMAL,
          PURUPURU_DIRECTION_LEFT, PURUPURU_DIRECTION_RIGHT },
        { PURUPURU_DIRECTION_NORMAL, PURUPURU_DIRECTION_FLIPPED,
          PURUPURU_DIRECTION_RIGHT, PURUPURU_DIRECTION_LEFT },
        { PURUPURU_DIRECTION_RIGHT, PURUPURU_DIRECTION_LEFT,
          PURUPURU_DIRECTION_FLIPPED, PURUPURU_DIRECTION_NORMAL },
        { PURUPURU_DIRECTION_LEFT, PURUPURU_DIRECTION_RIGHT,
          PURUPURU_DIRECTION_NORMAL, PURUPURU_DIRECTION_FLIPPED }
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

    if(!dev->valid || !(dev->info.functions & MAPLE_FUNC_PURUPURU)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    parent = maple_enum_dev(dev->port, 0);

    /* Root peripherals publish two socket directions. Standard attached
       devices occupy unit one or two, which map to those socket indices. */
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

int purupuru_is_ready(const maple_device_t *dev) {
    irq_mask_t irq;
    int ready;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!purupuru_device_valid(dev)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    ready = dev->frame.state == MAPLE_FRAME_VACANT;
    irq_restore(irq);
    return ready;
}

int purupuru_get_status(const maple_device_t *dev,
                        purupuru_status_t *status) {
    const purupuru_driver_status_t *driver_status;
    irq_mask_t irq;

    if(!dev || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!purupuru_device_valid(dev)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    driver_status = dev->status;
    *status = driver_status->public;
    irq_restore(irq);
    return 0;
}

int purupuru_set_completion_handler(maple_device_t *dev,
                                    purupuru_completion_handler_t handler,
                                    void *user_data) {
    purupuru_driver_status_t *status;
    irq_mask_t irq;

    if(!dev) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!purupuru_device_valid(dev)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    status = dev->status;
    status->completion_handler = handler;
    status->completion_data = user_data;
    irq_restore(irq);
    return 0;
}

static void purupuru_output_reply(maple_state_t *state, maple_frame_t *frame) {
    maple_response_t *response = (maple_response_t *)frame->recv_buf;
    purupuru_driver_status_t *status = frame->dev ? frame->dev->status : NULL;
    purupuru_completion_handler_t handler = NULL;
    void *handler_data = NULL;
    uint32_t sequence = 0;
    int result = MAPLE_EFAIL;

    (void)state;

    if(status) {
        result = response->response == MAPLE_RESPONSE_OK ?
                 MAPLE_EOK : MAPLE_EFAIL;
        status->public.busy = false;
        status->public.result = result;
        status->public.response = response->response;
        status->public.completed_sequence =
            status->public.submitted_sequence;
        sequence = status->public.completed_sequence;
        handler = status->completion_handler;
        handler_data = status->completion_data;
    }

    /* Unlock before the optional IRQ callback so a bounded callback may queue
       the next effect without manufacturing a false busy result. */
    maple_frame_unlock(frame);

    if(handler && frame->dev)
        handler(frame->dev, result, response->response, sequence,
                handler_data);
}

static int purupuru_submit(maple_device_t *dev, int command,
                           const uint32_t *payload, size_t payload_words,
                           uint8_t effect_count) {
    purupuru_driver_status_t *status;
    irq_mask_t irq;

    if(!purupuru_device_valid(dev))
        return MAPLE_EINVALID;

    if(maple_frame_trylock(&dev->frame) < 0)
        return MAPLE_EAGAIN;

    status = dev->status;
    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_PURUPURU;

    if(payload_words)
        memcpy(dev->frame.send_buf + 1, payload,
               payload_words * sizeof(*payload));

    dev->frame.cmd = command;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 1 + payload_words;
    dev->frame.callback = purupuru_output_reply;

    irq = irq_disable();
    status->public.busy = true;
    status->public.result = MAPLE_EAGAIN;
    status->public.response = MAPLE_RESPONSE_NONE;
    status->public.command = command;
    status->public.effect_count = effect_count;
    status->public.submitted_sequence =
        next_sequence(status->public.submitted_sequence);
    irq_restore(irq);

    if(maple_queue_frame(&dev->frame) < 0) {
        irq = irq_disable();
        status->public.busy = false;
        status->public.result = MAPLE_EFAIL;
        status->public.completed_sequence =
            status->public.submitted_sequence;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        irq_restore(irq);
        return MAPLE_EFAIL;
    }

    return MAPLE_EOK;
}

static bool effect_is_valid(const purupuru_effect_t *effect,
                            const purupuru_info_t *info,
                            uint16_t *seen_units) {
    const uint8_t unit = (effect->raw >> 4) & 0x0f;
    const bool ramp_up = effect->raw & 0x00000800;
    const bool ramp_down = effect->raw & 0x00008000;

    if(!unit || unit > info->units || (ramp_up && ramp_down) ||
       (*seen_units & (1u << unit))) {
        return false;
    }

    *seen_units |= 1u << unit;
    return true;
}

int purupuru_rumble_many(maple_device_t *dev,
                         const purupuru_effect_t *effects, size_t count) {
    uint32_t payload[PURUPURU_MAX_UNITS];
    purupuru_info_t info;
    uint16_t seen_units = 0;
    size_t i;

    if(!dev || !effects || !count || count > PURUPURU_MAX_UNITS ||
       purupuru_get_info(dev, &info) < 0 || count > info.simultaneous_units) {
        return MAPLE_EINVALID;
    }

    for(i = 0; i < count; ++i) {
        if(!effect_is_valid(&effects[i], &info, &seen_units))
            return MAPLE_EINVALID;

        payload[i] = effects[i].raw;
    }

    return purupuru_submit(dev, MAPLE_COMMAND_SETCOND, payload, count, count);
}

int purupuru_rumble(maple_device_t *dev, const purupuru_effect_t *effect) {
    if(!effect)
        return MAPLE_EINVALID;

    return purupuru_rumble_many(dev, effect, 1);
}

int purupuru_rumble_raw(maple_device_t *dev, uint32_t effect) {
    return purupuru_submit(dev, MAPLE_COMMAND_SETCOND, &effect, 1, 1);
}

int purupuru_stop(maple_device_t *dev, uint8_t unit) {
    purupuru_effect_config_t config = {
        .unit = unit,
        .power = 0,
        .frequency = 0,
        .cycles = 0,
        .continuous = false,
        .ramp = PURUPURU_RAMP_NONE
    };
    purupuru_effect_t effect;

    if(purupuru_effect_encode(&config, &effect) < 0)
        return MAPLE_EINVALID;

    return purupuru_rumble_many(dev, &effect, 1);
}

int purupuru_set_autostop_times(maple_device_t *dev, const uint8_t *units,
                                const uint8_t *times, size_t count) {
    uint32_t payload[1 + ((2 + PURUPURU_MAX_UNITS + 3) / 4)] = { 0 };
    uint8_t *data = (uint8_t *)(payload + 1);
    purupuru_info_t info;
    uint16_t mask = 0;
    size_t data_words;
    size_t i;

    if(!dev || !units || !times || !count ||
       count > PURUPURU_MAX_UNITS || purupuru_get_info(dev, &info) < 0 ||
       count > info.units) {
        return MAPLE_EINVALID;
    }

    for(i = 0; i < count; ++i) {
        if(!units[i] || units[i] > info.units ||
           (mask & (1u << units[i]))) {
            return MAPLE_EINVALID;
        }

        mask |= 1u << units[i];
        data[2 + i] = times[i];
    }

    /* BWRITE uses a zero location word followed by a big-endian unit mask and
       one quarter-second time byte for each selected unit, in unit-array order. */
    data[0] = mask >> 8;
    data[1] = mask & 0xff;
    data_words = (2 + count + 3) / 4;
    return purupuru_submit(dev, MAPLE_COMMAND_BWRITE, payload,
                           1 + data_words, count);
}

int purupuru_set_autostop_time(maple_device_t *dev, uint8_t unit,
                               uint8_t time) {
    return purupuru_set_autostop_times(dev, &unit, &time, 1);
}

static void purupuru_query_reply(maple_state_t *state, maple_frame_t *frame) {
    maple_response_t *response = (maple_response_t *)frame->recv_buf;
    const uint32_t *response_data = (const uint32_t *)response->data;
    purupuru_query_context_t *query =
        active_queries[frame->dst_port][frame->dst_unit];

    (void)state;

    if(query) {
        query->response = response->response;
        query->data_len = response->data_len;

        if(response->data_len >= 1)
            query->function = response_data[0];

        if(response->data_len >= 2)
            query->unit_info = response_data[1];

        query->done = true;
        active_queries[frame->dst_port][frame->dst_unit] = NULL;
    }

    /* The response is copied before release because unlocking permits another
       thread to reuse and overwrite this device's shared receive buffer. */
    maple_frame_unlock(frame);

    if(query)
        genwait_wake_all(query);
}

int purupuru_get_unit_info(maple_device_t *dev, uint8_t unit,
                           purupuru_unit_info_t *info) {
    purupuru_query_context_t query = { 0 };
    purupuru_info_t device_info;
    uint32_t raw;
    uint8_t unit_byte;
    uint8_t capabilities;
    uint8_t frequency_mode;
    irq_mask_t irq;

    if(!dev || !info) {
        errno = EINVAL;
        return -1;
    }

    if(purupuru_get_info(dev, &device_info) < 0)
        return -1;

    if(!unit || unit > device_info.units) {
        errno = EINVAL;
        return -1;
    }

    if(maple_frame_trylock(&dev->frame) < 0) {
        errno = EBUSY;
        return -1;
    }

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_PURUPURU;
    dev->frame.send_buf[1] = unit;
    dev->frame.cmd = MAPLE_COMMAND_GETMINFO;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = purupuru_query_reply;

    irq = irq_disable();
    active_queries[dev->port][dev->unit] = &query;
    irq_restore(irq);

    if(maple_queue_frame(&dev->frame) < 0) {
        irq = irq_disable();
        active_queries[dev->port][dev->unit] = NULL;
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        irq_restore(irq);
        errno = EIO;
        return -1;
    }

    if(genwait_wait(&query, "purupuru_get_unit_info", 100) < 0) {
        irq = irq_disable();

        if(!query.done) {
            if(active_queries[dev->port][dev->unit] == &query)
                active_queries[dev->port][dev->unit] = NULL;

            irq_restore(irq);
            errno = ETIMEDOUT;
            return -1;
        }

        irq_restore(irq);
    }

    if(query.response != MAPLE_RESPONSE_DATATRF || query.data_len < 2 ||
       query.function != MAPLE_FUNC_PURUPURU) {
        errno = EPROTO;
        return -1;
    }

    raw = query.unit_info;
    unit_byte = raw & 0xff;
    capabilities = (raw >> 8) & 0xff;

    if((unit_byte >> 4) != unit) {
        errno = EPROTO;
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->unit = unit_byte >> 4;
    info->position = (purupuru_unit_position_t)((unit_byte >> 2) & 0x03);
    info->axis = (purupuru_unit_axis_t)(unit_byte & 0x03);
    info->variable_power = capabilities & 0x80;
    info->continuous = capabilities & 0x40;
    info->directional = capabilities & 0x20;
    info->arbitrary_waveform = capabilities & 0x10;
    frequency_mode = capabilities & 0x0f;

    switch(frequency_mode) {
        case PURUPURU_FREQUENCY_RANGE:
            info->frequency_mode = PURUPURU_FREQUENCY_RANGE;
            info->minimum_frequency = (raw >> 16) & 0xff;
            info->maximum_frequency = raw >> 24;
            break;
        case PURUPURU_FREQUENCY_FIXED:
            info->frequency_mode = PURUPURU_FREQUENCY_FIXED;
            info->minimum_frequency = (raw >> 16) & 0xff;
            info->maximum_frequency = info->minimum_frequency;
            break;
        case PURUPURU_FREQUENCY_NONE:
            info->frequency_mode = PURUPURU_FREQUENCY_NONE;
            break;
        default:
            info->frequency_mode = PURUPURU_FREQUENCY_UNKNOWN;
            info->minimum_frequency = (raw >> 16) & 0xff;
            info->maximum_frequency = raw >> 24;
            break;
    }

    return 0;
}

static int purupuru_attach(maple_driver_t *driver, maple_device_t *dev) {
    purupuru_driver_status_t *status = dev->status;

    (void)driver;
    status->public.result = MAPLE_EOK;
    status->public.response = MAPLE_RESPONSE_NONE;
    return 0;
}

static maple_driver_t purupuru_drv = {
    .functions = MAPLE_FUNC_PURUPURU,
    .name = "Vibration Pack Driver",
    .status_size = sizeof(purupuru_driver_status_t),
    .attach = purupuru_attach
};

void purupuru_init(void) {
    maple_driver_reg(&purupuru_drv);
}

void purupuru_shutdown(void) {
    maple_driver_unreg(&purupuru_drv);
}
