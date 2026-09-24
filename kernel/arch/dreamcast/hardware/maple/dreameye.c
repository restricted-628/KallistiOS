/* KallistiOS ##version##

   dreameye.c
   Copyright (C) 2005, 2009 Lawrence Sebald
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kos/irq.h>
#include <kos/thread.h>
#include <dc/maple.h>
#include <dc/maple/dreameye.h>

#define DREAMEYE_COMMAND_TIMEOUT_MS  500u
#define DREAMEYE_DRAIN_TIMEOUT_MS    500u
#define DREAMEYE_CHUNK_BYTES         512u
#define DREAMEYE_MAX_CHUNKS          256u
#define DREAMEYE_TRANSFER_LANES      5u

typedef struct dreameye_driver_state {
    /* Preserve the status prefix returned by legacy maple_dev_status() use. */
    dreameye_state_t public;
    dreameye_operation_t pending_operation;
    dreameye_operation_t last_operation;
    dreameye_transfer_state_t transfer_state;
    int last_result;
    int last_response;
    uint32_t device_error;
    uint32_t sequence;
    uint16_t chunks_received;
    size_t bytes_received;
    uint32_t malformed_responses;
    uint32_t command_failures;
    uint32_t command_timeouts;
} dreameye_driver_state_t;

_Static_assert(offsetof(dreameye_driver_state_t, public) == 0,
               "legacy camera status must remain the first member");

typedef struct dreameye_transfer {
    maple_device_t *root;
    uint8_t *buffer;
    size_t capacity;
    size_t image_size;
    size_t bytes_received;
    uint16_t transfer_count;
    uint16_t chunks_received;
    uint16_t outstanding;
    uint8_t received[DREAMEYE_MAX_CHUNKS / 8];
    uint8_t expected_chunk[MAPLE_UNIT_COUNT];
    int result;
    int last_response;
    bool active;
    bool done;
    bool terminal_seen;
    bool cancelled;
    bool orphaned;
    bool disconnected;
    bool result_published;
} dreameye_transfer_t;

typedef struct dreameye_wait_context {
    maple_device_t *dev;
    dreameye_operation_t operation;
} dreameye_wait_context_t;

static dreameye_transfer_t transfers[MAPLE_PORT_COUNT];

static void dreameye_get_image_cb(maple_state_t *state,
                                  maple_frame_t *frame);

static dreameye_driver_state_t *dreameye_device_state(maple_device_t *dev) {
    if(!dev || !dev->valid || !dev->status || !dev->drv ||
       !(dev->info.functions & MAPLE_FUNC_CAMERA))
        return NULL;

    return dev->status;
}

static void dreameye_publish_result(dreameye_driver_state_t *state,
                                    dreameye_operation_t operation,
                                    int result, int response,
                                    uint32_t device_error) {
    if(!state)
        return;

    state->pending_operation = DREAMEYE_OPERATION_NONE;
    state->last_operation = operation;
    state->last_result = result;
    state->last_response = response;
    state->device_error = device_error;

    if(result != MAPLE_EOK)
        ++state->command_failures;

    if(++state->sequence == 0)
        ++state->sequence;
}

static void dreameye_publish_transfer(dreameye_transfer_t *transfer,
                                      dreameye_transfer_state_t state,
                                      int result, int response) {
    dreameye_driver_state_t *device_state;
    irq_mask_t irq = irq_disable();

    device_state = dreameye_device_state(transfer->root);

    if(!device_state) {
        irq_restore(irq);
        return;
    }

    device_state->public.transfer_count = transfer->transfer_count;
    device_state->public.img_transferring =
        state == DREAMEYE_TRANSFER_RECEIVING;
    device_state->public.img_size = (int)transfer->image_size;
    device_state->transfer_state = state;
    device_state->chunks_received = transfer->chunks_received;
    device_state->bytes_received = transfer->bytes_received;

    if((state == DREAMEYE_TRANSFER_COMPLETE ||
        state == DREAMEYE_TRANSFER_ERROR ||
        state == DREAMEYE_TRANSFER_DISCONNECTED) &&
       !transfer->result_published) {
        dreameye_publish_result(device_state, DREAMEYE_OPERATION_IMAGE_READ,
                                result, response, 0);
        transfer->result_published = true;
    }

    irq_restore(irq);
}

static void dreameye_mark_malformed(dreameye_transfer_t *transfer) {
    dreameye_driver_state_t *state = dreameye_device_state(transfer->root);

    if(state)
        ++state->malformed_responses;
}

static bool dreameye_chunk_received(const dreameye_transfer_t *transfer,
                                    unsigned int chunk) {
    return (transfer->received[chunk / 8] & (1u << (chunk & 7))) != 0;
}

static void dreameye_mark_chunk_received(dreameye_transfer_t *transfer,
                                         unsigned int chunk) {
    transfer->received[chunk / 8] |= (uint8_t)(1u << (chunk & 7));
}

static int dreameye_wait_command_done(void *data) {
    dreameye_wait_context_t *wait = data;
    dreameye_driver_state_t *state;

    if(!wait->dev->valid || !wait->dev->status)
        return 1;

    state = wait->dev->status;
    return state->pending_operation != wait->operation;
}

static int dreameye_wait_command(maple_device_t *dev,
                                 dreameye_operation_t operation) {
    dreameye_wait_context_t wait = { dev, operation };
    dreameye_driver_state_t *state;
    irq_mask_t irq;
    int result;

    if(!thd_poll(dreameye_wait_command_done, &wait,
                 DREAMEYE_COMMAND_TIMEOUT_MS)) {
        irq = irq_disable();
        state = dreameye_device_state(dev);

        if(state && state->pending_operation == operation) {
            state->last_operation = operation;
            state->last_result = MAPLE_ETIMEOUT;
            state->last_response = MAPLE_RESPONSE_NONE;
            ++state->command_timeouts;

            if(++state->sequence == 0)
                ++state->sequence;
        }

        irq_restore(irq);
        return MAPLE_ETIMEOUT;
    }

    irq = irq_disable();
    state = dreameye_device_state(dev);
    result = state ? state->last_result : MAPLE_EFAIL;
    irq_restore(irq);
    return result;
}

static int dreameye_begin_command(maple_device_t *dev,
                                  dreameye_operation_t operation,
                                  uint32_t command, uint32_t argument,
                                  void (*callback)(maple_state_t *,
                                                   maple_frame_t *)) {
    dreameye_driver_state_t *state;
    dreameye_transfer_t *transfer;
    irq_mask_t irq;

    irq = irq_disable();
    state = dreameye_device_state(dev);

    if(!state) {
        irq_restore(irq);
        return MAPLE_EINVALID;
    }

    transfer = &transfers[dev->port];

    if(state->pending_operation != DREAMEYE_OPERATION_NONE ||
       transfer->active) {
        irq_restore(irq);
        return MAPLE_EAGAIN;
    }

    if(maple_frame_trylock(&dev->frame) < 0) {
        irq_restore(irq);
        return MAPLE_EAGAIN;
    }

    state->pending_operation = operation;
    state->last_response = MAPLE_RESPONSE_NONE;

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_CAMERA;
    dev->frame.send_buf[1] = argument;
    dev->frame.cmd = command;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = callback;

    if(maple_queue_frame(&dev->frame) < 0) {
        dev->frame.state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(&dev->frame);
        dreameye_publish_result(state, operation, MAPLE_EFAIL,
                                MAPLE_RESPONSE_NONE, 0);
        irq_restore(irq);
        return MAPLE_EFAIL;
    }

    irq_restore(irq);
    return MAPLE_EOK;
}

static bool dreameye_decode_getcond(maple_frame_t *frame,
                                    uint8_t expected_condition,
                                    uint16_t *value) {
    const maple_response_t *response =
        (const maple_response_t *)frame->recv_buf;
    const uint32_t *words = (const uint32_t *)response->data;
    const uint8_t *bytes = response->data;

    if(response->response != MAPLE_RESPONSE_DATATRF ||
       response->data_len != 3 || words[0] != MAPLE_FUNC_CAMERA ||
       bytes[4] != 0xd0 || bytes[5] != 0x00 ||
       bytes[8] != expected_condition)
        return false;

    *value = (uint16_t)((bytes[10] << 8) | bytes[11]);
    return true;
}

static void dreameye_get_image_count_cb(maple_state_t *maple,
                                        maple_frame_t *frame) {
    dreameye_driver_state_t *state =
        frame->dev ? dreameye_device_state(frame->dev) : NULL;
    const maple_response_t *response =
        (const maple_response_t *)frame->recv_buf;
    uint16_t count = 0;
    int result = MAPLE_EFAIL;

    (void)maple;

    if(state && dreameye_decode_getcond(frame,
                                        DREAMEYE_GETCOND_NUM_IMAGES,
                                        &count) &&
       ((const uint8_t *)response->data)[9] == 0x04) {
        state->public.image_count = count;
        state->public.image_count_valid = 1;
        result = MAPLE_EOK;
    }
    else if(state) {
        state->public.image_count_valid = 0;
        ++state->malformed_responses;
    }

    if(state)
        dreameye_publish_result(state, DREAMEYE_OPERATION_IMAGE_COUNT,
                                result, response->response, 0);

    maple_frame_unlock(frame);
}

static void dreameye_get_transfer_count_cb(maple_state_t *maple,
                                           maple_frame_t *frame) {
    dreameye_driver_state_t *state =
        frame->dev ? dreameye_device_state(frame->dev) : NULL;
    const maple_response_t *response =
        (const maple_response_t *)frame->recv_buf;
    uint16_t count = 0;
    int result = MAPLE_EFAIL;

    (void)maple;

    if(state && dreameye_decode_getcond(frame,
                                        DREAMEYE_GETCOND_TRANSFER_COUNT,
                                        &count) && count != 0 && count <= 256) {
        state->public.transfer_count = count;
        result = MAPLE_EOK;
    }
    else if(state) {
        state->public.transfer_count = 0;
        ++state->malformed_responses;
    }

    if(state)
        dreameye_publish_result(state,
                                DREAMEYE_OPERATION_TRANSFER_COUNT,
                                result, response->response, 0);

    maple_frame_unlock(frame);
}

int dreameye_get_image_count(maple_device_t *dev, int block) {
    dreameye_driver_state_t *state;
    irq_mask_t irq;
    int result;

    irq = irq_disable();
    state = dreameye_device_state(dev);

    if(!state) {
        irq_restore(irq);
        return MAPLE_EINVALID;
    }

    state->public.image_count_valid = 0;
    irq_restore(irq);
    result = dreameye_begin_command(dev, DREAMEYE_OPERATION_IMAGE_COUNT,
                                    MAPLE_COMMAND_GETCOND,
                                    DREAMEYE_GETCOND_NUM_IMAGES | (0x04 << 8),
                                    dreameye_get_image_count_cb);

    if(result != MAPLE_EOK || !block)
        return result;

    return dreameye_wait_command(dev, DREAMEYE_OPERATION_IMAGE_COUNT);
}

int dreameye_get_image_transfer_count(maple_device_t *dev, uint8_t image,
                                      uint16_t *transfer_count) {
    dreameye_driver_state_t *state;
    irq_mask_t irq;
    int result;

    if(!transfer_count || image < 0x02 || image > 0x21)
        return MAPLE_EINVALID;

    *transfer_count = 0;
    irq = irq_disable();
    state = dreameye_device_state(dev);

    if(!state) {
        irq_restore(irq);
        return MAPLE_EINVALID;
    }

    state->public.transfer_count = 0;
    irq_restore(irq);
    result = dreameye_begin_command(dev,
                                    DREAMEYE_OPERATION_TRANSFER_COUNT,
                                    MAPLE_COMMAND_GETCOND,
                                    DREAMEYE_GETCOND_TRANSFER_COUNT |
                                    (image << 8),
                                    dreameye_get_transfer_count_cb);

    if(result != MAPLE_EOK)
        return result;

    result = dreameye_wait_command(dev,
                                   DREAMEYE_OPERATION_TRANSFER_COUNT);

    if(result == MAPLE_EOK) {
        irq = irq_disable();
        state = dreameye_device_state(dev);

        if(state)
            *transfer_count = (uint16_t)state->public.transfer_count;
        else
            result = MAPLE_EFAIL;

        irq_restore(irq);
    }

    return result;
}

static int dreameye_send_image_request(dreameye_transfer_t *transfer,
                                       maple_device_t *dev,
                                       uint8_t request, uint8_t chunk) {
    dreameye_driver_state_t *state =
        dreameye_device_state(transfer->root);
    maple_frame_t *frame = &dev->frame;

    if(!state || !dev->valid || dev->port != transfer->root->port ||
       !(dev->info.functions & MAPLE_FUNC_CAMERA))
        return MAPLE_EFAIL;

    if(maple_frame_trylock(frame) < 0)
        return MAPLE_EAGAIN;

    maple_frame_init(frame);
    frame->send_buf[0] = MAPLE_FUNC_CAMERA;
    frame->send_buf[1] = DREAMEYE_SUBCOMMAND_IMAGEREQ |
                         (state->public.img_number << 8) |
                         (request << 16) | ((uint32_t)chunk << 24);
    frame->cmd = MAPLE_COMMAND_CAMCONTROL;
    frame->dst_port = dev->port;
    frame->dst_unit = dev->unit;
    frame->length = 2;
    frame->callback = dreameye_get_image_cb;
    transfer->expected_chunk[dev->unit] = chunk;

    if(maple_queue_frame(frame) < 0) {
        frame->state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(frame);
        return MAPLE_EFAIL;
    }

    ++transfer->outstanding;
    return MAPLE_EOK;
}

static void dreameye_finish_transfer_if_drained(
    dreameye_transfer_t *transfer) {
    dreameye_transfer_state_t state;

    if(transfer->done || transfer->outstanding != 0)
        return;

    if(!transfer->cancelled && transfer->terminal_seen &&
       transfer->chunks_received == transfer->transfer_count) {
        transfer->result = MAPLE_EOK;
        state = DREAMEYE_TRANSFER_COMPLETE;
    }
    else {
        if(transfer->result == MAPLE_EOK)
            transfer->result = MAPLE_EFAIL;

        state = transfer->disconnected ? DREAMEYE_TRANSFER_DISCONNECTED :
                                        DREAMEYE_TRANSFER_ERROR;
    }

    transfer->done = true;
    dreameye_publish_transfer(transfer, state, transfer->result,
                              transfer->last_response);
}

static void dreameye_fail_transfer(dreameye_transfer_t *transfer,
                                   int result, int response,
                                   bool malformed) {
    if(transfer->cancelled)
        return;

    transfer->cancelled = true;
    transfer->result = result;
    transfer->last_response = response;

    if(malformed)
        dreameye_mark_malformed(transfer);
}

static void dreameye_get_image_cb(maple_state_t *maple,
                                  maple_frame_t *frame) {
    dreameye_transfer_t *transfer;
    const maple_response_t *response =
        (const maple_response_t *)frame->recv_buf;
    const uint32_t *words = (const uint32_t *)response->data;
    const uint8_t *bytes = response->data;
    maple_device_t *dev = frame->dev;
    size_t payload = 0;
    unsigned int chunk = 0;
    unsigned int next = DREAMEYE_MAX_CHUNKS;
    bool schedule_next = false;

    (void)maple;

    if(frame->dst_port < 0 || frame->dst_port >= MAPLE_PORT_COUNT) {
        maple_frame_unlock(frame);
        return;
    }

    transfer = &transfers[frame->dst_port];

    if(transfer->active && transfer->outstanding != 0)
        --transfer->outstanding;

    if(!transfer->active || transfer->cancelled || !dev) {
        maple_frame_unlock(frame);
        dreameye_finish_transfer_if_drained(transfer);
        return;
    }

    if(response->response != MAPLE_RESPONSE_DATATRF ||
       response->data_len < 3 || words[0] != MAPLE_FUNC_CAMERA ||
       bytes[4] == DREAMEYE_SUBCOMMAND_ERROR) {
        dreameye_fail_transfer(transfer, MAPLE_EFAIL,
                               response->response, true);
    }
    else {
        chunk = bytes[5];
        payload = (size_t)(response->data_len - 3) * sizeof(uint32_t);

        if(frame->dst_unit < 0 || frame->dst_unit >= MAPLE_UNIT_COUNT ||
           chunk != transfer->expected_chunk[frame->dst_unit] ||
           chunk >= transfer->transfer_count ||
           payload > DREAMEYE_CHUNK_BYTES || payload > transfer->capacity ||
           (size_t)chunk * DREAMEYE_CHUNK_BYTES >
               transfer->capacity - payload ||
           dreameye_chunk_received(transfer, chunk)) {
            dreameye_fail_transfer(transfer, MAPLE_EFAIL,
                                   response->response, true);
        }
        else {
            memcpy(transfer->buffer + chunk * DREAMEYE_CHUNK_BYTES,
                   bytes + 12, payload);
            dreameye_mark_chunk_received(transfer, chunk);
            ++transfer->chunks_received;
            transfer->bytes_received += payload;

            if(transfer->image_size < chunk * DREAMEYE_CHUNK_BYTES + payload)
                transfer->image_size = chunk * DREAMEYE_CHUNK_BYTES + payload;

            transfer->last_response = response->response;

            /* The high bit marks the final response, despite sharing the same
               value used to request that the first lane start at chunk zero. */
            if(bytes[4] & DREAMEYE_IMAGEREQ_START) {
                if(chunk + 1 != transfer->transfer_count)
                    dreameye_fail_transfer(transfer, MAPLE_EFAIL,
                                           response->response, true);
                else
                    transfer->terminal_seen = true;
            }

            next = chunk + DREAMEYE_TRANSFER_LANES;
            /* Lanes complete out of order. Seeing the final chunk on one lane
               must not prevent a slower lane from requesting its remaining
               lower-numbered chunks. */
            schedule_next = !transfer->cancelled &&
                            next < transfer->transfer_count;
        }
    }

    /* A frame becomes reusable only after its response has been consumed. */
    maple_frame_unlock(frame);

    if(schedule_next &&
       dreameye_send_image_request(transfer, dev,
                                   DREAMEYE_IMAGEREQ_CONTINUE,
                                   (uint8_t)next) != MAPLE_EOK) {
        dreameye_fail_transfer(transfer, MAPLE_EFAIL,
                               MAPLE_RESPONSE_NONE, false);
    }

    dreameye_publish_transfer(transfer, DREAMEYE_TRANSFER_RECEIVING,
                              MAPLE_EOK, transfer->last_response);
    dreameye_finish_transfer_if_drained(transfer);
}

static void dreameye_cancel_unsent(dreameye_transfer_t *transfer) {
    unsigned int unit;

    if(!transfer->root)
        return;

    for(unit = 1; unit < MAPLE_UNIT_COUNT; ++unit) {
        maple_device_t *dev =
            maple_state.ports[transfer->root->port].units[unit];
        maple_frame_t *frame;

        if(!dev)
            continue;

        frame = &dev->frame;

        if(frame->callback != dreameye_get_image_cb ||
           frame->dst_port != transfer->root->port ||
           frame->state != MAPLE_FRAME_UNSENT || !frame->queued)
            continue;

        maple_queue_remove(frame);
        frame->state = MAPLE_FRAME_RESPONDED;
        maple_frame_unlock(frame);

        if(transfer->outstanding != 0)
            --transfer->outstanding;
    }

    dreameye_finish_transfer_if_drained(transfer);
}

static int dreameye_transfer_done(void *data) {
    dreameye_transfer_t *transfer = data;

    if(transfer->cancelled)
        dreameye_cancel_unsent(transfer);

    return transfer->done;
}

static void dreameye_release_transfer(dreameye_transfer_t *transfer,
                                      bool retain_buffer) {
    maple_device_t *root = transfer->root;
    uint8_t *buffer = transfer->buffer;
    dreameye_driver_state_t *state;
    irq_mask_t irq;

    irq = irq_disable();
    state = dreameye_device_state(root);

    if(state && state->public.img_buf == buffer)
        state->public.img_buf = NULL;

    irq_restore(irq);

    memset(transfer, 0, sizeof(*transfer));

    if(!retain_buffer)
        free(buffer);
}

static void dreameye_reap_transfer(unsigned int port) {
    dreameye_transfer_t *transfer = &transfers[port];
    irq_mask_t irq;
    bool reap;

    irq = irq_disable();
    reap = transfer->active && transfer->orphaned && transfer->done &&
           transfer->outstanding == 0;
    irq_restore(irq);

    if(reap)
        dreameye_release_transfer(transfer, false);
}

static int dreameye_validate_transfer_devices(maple_device_t *root,
                                              uint16_t transfer_count,
                                              maple_device_t **devices,
                                              unsigned int *device_count) {
    unsigned int count = transfer_count < DREAMEYE_TRANSFER_LANES ?
                         transfer_count : DREAMEYE_TRANSFER_LANES;
    unsigned int unit;

    for(unit = 1; unit <= count; ++unit) {
        maple_device_t *dev = maple_enum_dev(root->port, unit);

        if(!dev || !(dev->info.functions & MAPLE_FUNC_CAMERA))
            return MAPLE_EINVALID;

        devices[unit - 1] = dev;
    }

    *device_count = count;
    return MAPLE_EOK;
}

int dreameye_get_image_timed(maple_device_t *dev, uint8_t image,
                             uint8_t **data, int *img_sz,
                             uint32_t timeout_ms) {
    dreameye_driver_state_t *state;
    dreameye_transfer_t *transfer;
    maple_device_t *devices[DREAMEYE_TRANSFER_LANES];
    uint16_t transfer_count;
    unsigned int device_count;
    unsigned int i;
    irq_mask_t irq;
    int result;

    if(data)
        *data = NULL;

    if(img_sz)
        *img_sz = 0;

    if(!dev || !data || !img_sz || timeout_ms == 0 || dev->unit != 1 ||
       image < 0x02 || image > 0x21)
        return MAPLE_EINVALID;

    irq = irq_disable();
    state = dreameye_device_state(dev);

    if(!state) {
        irq_restore(irq);
        return MAPLE_EINVALID;
    }

    state->transfer_state = DREAMEYE_TRANSFER_QUERYING;
    irq_restore(irq);
    dreameye_reap_transfer(dev->port);
    result = dreameye_get_image_transfer_count(dev, image, &transfer_count);

    if(result != MAPLE_EOK) {
        irq = irq_disable();
        state = dreameye_device_state(dev);

        if(state)
            state->transfer_state = DREAMEYE_TRANSFER_ERROR;

        irq_restore(irq);
        return result;
    }

    result = dreameye_validate_transfer_devices(dev, transfer_count,
                                                devices, &device_count);

    if(result != MAPLE_EOK) {
        irq = irq_disable();
        state = dreameye_device_state(dev);

        if(state)
            state->transfer_state = DREAMEYE_TRANSFER_ERROR;

        irq_restore(irq);
        return result;
    }

    transfer = &transfers[dev->port];
    irq = irq_disable();

    state = dreameye_device_state(dev);

    if(!state || transfer->active ||
       state->pending_operation != DREAMEYE_OPERATION_NONE) {
        irq_restore(irq);
        return MAPLE_EAGAIN;
    }

    memset(transfer, 0, sizeof(*transfer));
    transfer->active = true;
    transfer->root = dev;
    transfer->transfer_count = transfer_count;
    transfer->capacity = (size_t)transfer_count * DREAMEYE_CHUNK_BYTES;
    transfer->result = MAPLE_EOK;
    transfer->last_response = MAPLE_RESPONSE_NONE;
    state->pending_operation = DREAMEYE_OPERATION_IMAGE_READ;
    state->transfer_state = DREAMEYE_TRANSFER_RECEIVING;
    state->public.transfer_count = transfer_count;
    state->public.img_transferring = 1;
    state->public.img_buf = NULL;
    state->public.img_size = 0;
    state->public.img_number = image;
    state->chunks_received = 0;
    state->bytes_received = 0;
    irq_restore(irq);

    transfer->buffer = malloc(transfer->capacity);

    if(!transfer->buffer) {
        transfer->cancelled = true;
        transfer->result = MAPLE_EFAIL;
        dreameye_finish_transfer_if_drained(transfer);
        dreameye_release_transfer(transfer, false);
        return MAPLE_EFAIL;
    }

    irq = irq_disable();
    state = dreameye_device_state(dev);

    if(!state || transfer->cancelled) {
        irq_restore(irq);
        dreameye_release_transfer(transfer, false);
        return MAPLE_EFAIL;
    }

    state->public.img_buf = transfer->buffer;
    irq_restore(irq);

    /* Stripe chunks across the camera's data units. Each unit owns one Maple
       frame and receives its next chunk only after its prior callback has
       consumed the response and released that frame. */
    irq = irq_disable();

    for(i = 0; i < device_count; ++i) {
        result = dreameye_send_image_request(
            transfer, devices[i],
            i == 0 ? DREAMEYE_IMAGEREQ_START :
                     DREAMEYE_IMAGEREQ_CONTINUE,
            (uint8_t)i);

        if(result != MAPLE_EOK) {
            dreameye_fail_transfer(transfer, result,
                                   MAPLE_RESPONSE_NONE, false);
            break;
        }
    }

    irq_restore(irq);

    if(transfer->cancelled)
        dreameye_cancel_unsent(transfer);

    if(!transfer->done && !thd_poll(dreameye_transfer_done, transfer,
                                    timeout_ms)) {
        irq = irq_disable();
        dreameye_fail_transfer(transfer, MAPLE_ETIMEOUT,
                               MAPLE_RESPONSE_NONE, false);
        state = dreameye_device_state(dev);

        if(state)
            ++state->command_timeouts;

        dreameye_publish_transfer(transfer, DREAMEYE_TRANSFER_ERROR,
                                  MAPLE_ETIMEOUT, MAPLE_RESPONSE_NONE);
        irq_restore(irq);

        /* Polling cancellation repeatedly removes frames which were UNSENT at
           the deadline or became UNSENT after a retry response. SENT frames
           retain the transfer buffer until their IRQ callback drains them. */
        if(!thd_poll(dreameye_transfer_done, transfer,
                     DREAMEYE_DRAIN_TIMEOUT_MS)) {
            irq = irq_disable();
            transfer->orphaned = true;
            irq_restore(irq);
            return MAPLE_ETIMEOUT;
        }
    }

    result = transfer->result;

    if(result == MAPLE_EOK) {
        *data = transfer->buffer;
        *img_sz = (int)transfer->image_size;
        dreameye_release_transfer(transfer, true);
    }
    else {
        dreameye_release_transfer(transfer, false);
    }

    return result;
}

int dreameye_get_image(maple_device_t *dev, uint8_t image, uint8_t **data,
                       int *img_sz) {
    return dreameye_get_image_timed(dev, image, data, img_sz,
                                    DREAMEYE_DEFAULT_TRANSFER_TIMEOUT);
}

static void dreameye_erase_cb(maple_state_t *maple, maple_frame_t *frame) {
    dreameye_driver_state_t *state =
        frame->dev ? dreameye_device_state(frame->dev) : NULL;
    const maple_response_t *response =
        (const maple_response_t *)frame->recv_buf;
    const uint8_t *bytes = response->data;
    uint32_t device_error = 0;
    int result = MAPLE_EFAIL;

    (void)maple;

    if(response->response == MAPLE_RESPONSE_OK) {
        result = MAPLE_EOK;
    }
    else if(response->response == MAPLE_COMMAND_CAMCONTROL &&
            response->data_len >= 2 &&
            bytes[4] == DREAMEYE_SUBCOMMAND_ERROR) {
        device_error = (bytes[5] << 16) | (bytes[6] << 8) | bytes[7];
    }
    else if(state) {
        ++state->malformed_responses;
    }

    if(state)
        dreameye_publish_result(state, DREAMEYE_OPERATION_ERASE,
                                result, response->response, device_error);

    maple_frame_unlock(frame);
}

int dreameye_erase_image(maple_device_t *dev, uint8_t image, int block) {
    int result;

    if(image < 0x02 || (image > 0x21 && image != 0xff))
        return MAPLE_EINVALID;

    result = dreameye_begin_command(
        dev, DREAMEYE_OPERATION_ERASE, MAPLE_COMMAND_CAMCONTROL,
        DREAMEYE_SUBCOMMAND_ERASE | (0x80 << 8) | (image << 16),
        dreameye_erase_cb);

    if(result != MAPLE_EOK || !block)
        return result;

    return dreameye_wait_command(dev, DREAMEYE_OPERATION_ERASE);
}

int dreameye_get_status(const maple_device_t *dev,
                        dreameye_status_t *status) {
    const dreameye_driver_state_t *state;
    irq_mask_t irq;

    if(!dev || !status) {
        errno = EINVAL;
        return -1;
    }

    irq = irq_disable();

    if(!dev->valid || !dev->status ||
       !(dev->info.functions & MAPLE_FUNC_CAMERA)) {
        irq_restore(irq);
        errno = ENODEV;
        return -1;
    }

    state = dev->status;
    status->pending_operation = state->pending_operation;
    status->last_operation = state->last_operation;
    status->transfer_state = state->transfer_state;
    status->last_result = state->last_result;
    status->last_response = state->last_response;
    status->device_error = state->device_error;
    status->sequence = state->sequence;
    status->image_count = state->public.image_count;
    status->image_count_valid = state->public.image_count_valid;
    status->transfer_count = (uint16_t)state->public.transfer_count;
    status->chunks_received = state->chunks_received;
    status->bytes_received = state->bytes_received;
    status->malformed_responses = state->malformed_responses;
    status->command_failures = state->command_failures;
    status->command_timeouts = state->command_timeouts;
    irq_restore(irq);
    return 0;
}

static int dreameye_attach(maple_driver_t *driver, maple_device_t *dev) {
    dreameye_driver_state_t *state = dev->status;

    (void)driver;

    state->last_result = MAPLE_EOK;
    state->last_response = MAPLE_RESPONSE_NONE;
    state->transfer_state = DREAMEYE_TRANSFER_IDLE;
    return 0;
}

static void dreameye_detach(maple_driver_t *driver, maple_device_t *dev) {
    dreameye_transfer_t *transfer;

    (void)driver;

    if(dev->port < 0 || dev->port >= MAPLE_PORT_COUNT)
        return;

    transfer = &transfers[dev->port];

    if(!transfer->active)
        return;

    transfer->disconnected = true;
    dreameye_fail_transfer(transfer, MAPLE_EFAIL,
                           MAPLE_RESPONSE_NONE, false);
    dreameye_cancel_unsent(transfer);
}

static maple_driver_t dreameye_drv = {
    .functions = MAPLE_FUNC_CAMERA,
    .name = "Dreameye (Camera)",
    .status_size = sizeof(dreameye_driver_state_t),
    .attach = dreameye_attach,
    .detach = dreameye_detach
};

void dreameye_init(void) {
    memset(transfers, 0, sizeof(transfers));
    maple_driver_reg(&dreameye_drv);
}

void dreameye_shutdown(void) {
    unsigned int port;

    for(port = 0; port < MAPLE_PORT_COUNT; ++port) {
        dreameye_transfer_t *transfer = &transfers[port];

        if(!transfer->active)
            continue;

        transfer->cancelled = true;
        transfer->result = MAPLE_EFAIL;
        (void)thd_poll(dreameye_transfer_done, transfer,
                       DREAMEYE_DRAIN_TIMEOUT_MS);

        if(transfer->done)
            dreameye_release_transfer(transfer, false);
        else
            transfer->orphaned = true;
    }

    maple_driver_unreg(&dreameye_drv);
}
