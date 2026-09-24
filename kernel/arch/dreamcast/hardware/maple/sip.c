/* KallistiOS ##version##

   sip.c
   Copyright (C) 2005, 2008, 2013 Lawrence Sebald
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <kos/genwait.h>
#include <kos/irq.h>

#include <dc/maple.h>
#include <dc/maple/sip.h>

#include "sip_stream_internal.h"

#define SIP_START_SAMPLING 0x80
#define SIP_COPY_CHUNK 256u

typedef enum sip_pending_command {
    SIP_COMMAND_NONE = 0,
    SIP_COMMAND_START,
    SIP_COMMAND_STOP
} sip_pending_command_t;

struct sip_stream {
    sip_capture_t *capture;
    struct sip_stream *next;
    uint64_t read_bytes;
    uint64_t lost_bytes;
    bool overrun;
};

struct sip_capture {
    maple_device_t *dev;
    uint8_t *buffer;
    size_t buffer_size;
    size_t sample_size;
    sip_capture_state_t state;
    uint64_t write_bytes;
    uint64_t packets_received;
    uint64_t samples_received;
    uint32_t sample_status;
    sip_stream_t *streams;
    size_t stream_count;
};

typedef struct sip_driver_state {
    /* Keep the established status layout at offset zero for
       maple_dev_status() callers. */
    sip_state_t public;
    sip_capture_t *capture;
    sip_pending_command_t pending;
    int command_result;
    int last_response;
    uint32_t sequence;
    uint64_t packets_received;
    uint64_t samples_received;
    uint64_t malformed_responses;
    uint64_t command_failures;
    uint64_t command_timeouts;
    uint32_t sample_status;
} sip_driver_state_t;

_Static_assert(offsetof(sip_driver_state_t, public) == 0,
               "sip_state_t must remain the public status prefix");

static sip_driver_state_t *sip_state_private(maple_device_t *dev) {
    if(!dev || !dev->valid || !dev->status ||
       !(dev->info.functions & MAPLE_FUNC_MICROPHONE))
        return NULL;

    return (sip_driver_state_t *)dev->status;
}

static sip_capture_state_t sip_device_capture_state(
    const sip_driver_state_t *sip) {
    if(sip->pending == SIP_COMMAND_START)
        return SIP_CAPTURE_STARTING;

    if(sip->pending == SIP_COMMAND_STOP)
        return SIP_CAPTURE_STOPPING;

    if(sip->public.is_sampling)
        return SIP_CAPTURE_RECORDING;

    if(sip->command_result != MAPLE_EOK)
        return SIP_CAPTURE_ERROR;

    return SIP_CAPTURE_STOPPED;
}

static int sip_response_result(int response) {
    if(response == MAPLE_RESPONSE_OK)
        return MAPLE_EOK;

    if(response == MAPLE_RESPONSE_AGAIN)
        return MAPLE_EAGAIN;

    return MAPLE_EFAIL;
}

static void sip_reset_capture_locked(sip_capture_t *capture) {
    sip_stream_t *stream;

    capture->write_bytes = 0;
    capture->packets_received = 0;
    capture->samples_received = 0;
    capture->sample_status = 0;

    for(stream = capture->streams; stream; stream = stream->next) {
        stream->read_bytes = 0;
        stream->lost_bytes = 0;
        stream->overrun = false;
    }
}

static void sip_control_reply(maple_state_t *st, maple_frame_t *frame) {
    sip_driver_state_t *sip;
    sip_pending_command_t command;
    maple_response_t *response;
    int result;

    (void)st;

    response = (maple_response_t *)frame->recv_buf;
    sip = frame->dev && frame->dev->status
        ? (sip_driver_state_t *)frame->dev->status : NULL;

    if(!sip) {
        maple_frame_unlock(frame);
        genwait_wake_all(frame);
        return;
    }

    command = sip->pending;
    result = sip_response_result(response->response);
    sip->last_response = response->response;
    sip->command_result = result;
    sip->pending = SIP_COMMAND_NONE;

    if(result == MAPLE_EOK) {
        if(command == SIP_COMMAND_START) {
            sip->public.is_sampling = true;

            if(sip->capture)
                sip->capture->state = SIP_CAPTURE_RECORDING;
        }
        else if(command == SIP_COMMAND_STOP) {
            sip->public.is_sampling = false;
            sip->public.callback = NULL;

            if(sip->capture)
                sip->capture->state = SIP_CAPTURE_STOPPED;
        }
    }
    else {
        ++sip->command_failures;

        if(command == SIP_COMMAND_START) {
            sip->public.callback = NULL;

            if(sip->capture)
                sip->capture->state = SIP_CAPTURE_ERROR;
        }
        else if(command == SIP_COMMAND_STOP && sip->capture) {
            sip->capture->state = SIP_CAPTURE_RECORDING;
        }
    }

    if(++sip->sequence == 0)
        ++sip->sequence;

    /* Publish the complete command result before releasing the frame and
       waking a blocking submitter. */
    maple_frame_unlock(frame);
    genwait_wake_all(frame);
}

static int sip_submit_control(maple_device_t *dev, sip_sample_cb callback,
                              sip_pending_command_t command, bool block) {
    sip_driver_state_t *sip;
    irq_mask_t old;
    int result;

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        return MAPLE_EINVALID;
    }

    if(sip->pending != SIP_COMMAND_NONE ||
       (command == SIP_COMMAND_START && sip->public.is_sampling) ||
       (command == SIP_COMMAND_STOP && !sip->public.is_sampling)) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    if(command == SIP_COMMAND_START && !callback && !sip->capture) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    if(maple_frame_trylock(&dev->frame) < 0) {
        irq_restore(old);
        return MAPLE_EAGAIN;
    }

    if(command == SIP_COMMAND_START) {
        sip->public.callback = callback;
        sip->command_result = MAPLE_EOK;

        if(sip->capture) {
            sip->capture->sample_size = sip->public.sample_type ==
                SIP_SAMPLE_16BIT_SIGNED ? 2u : 1u;
            sip_reset_capture_locked(sip->capture);
            sip->capture->state = SIP_CAPTURE_STARTING;
        }
    }
    else if(sip->capture) {
        sip->capture->state = SIP_CAPTURE_STOPPING;
    }

    sip->pending = command;

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_MICROPHONE;
    dev->frame.send_buf[1] = SIP_SUBCOMMAND_BASIC_CTRL;

    if(command == SIP_COMMAND_START) {
        dev->frame.send_buf[1] |=
            ((sip->public.sample_type |
              (sip->public.frequency << 2) |
              SIP_START_SAMPLING) << 8);
    }

    dev->frame.cmd = MAPLE_COMMAND_MICCONTROL;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = sip_control_reply;
    maple_queue_frame(&dev->frame);
    irq_restore(old);

    if(!block)
        return MAPLE_EOK;

    if(genwait_wait(&dev->frame,
                    command == SIP_COMMAND_START
                        ? "sip_start_sampling" : "sip_stop_sampling",
                    500) < 0) {
        old = irq_disable();
        sip = sip_state_private(dev);

        if(!sip) {
            irq_restore(old);
            return MAPLE_EFAIL;
        }

        if(sip->pending == command) {
            ++sip->command_timeouts;
            irq_restore(old);
            return MAPLE_ETIMEOUT;
        }

        result = sip->command_result;
        irq_restore(old);
        return result;
    }

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    result = sip->command_result;
    irq_restore(old);
    return result;
}

int sip_set_gain(maple_device_t *dev, unsigned int gain) {
    sip_driver_state_t *sip;
    irq_mask_t old;

    if(gain > SIP_MAX_GAIN)
        return MAPLE_EINVALID;

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        return MAPLE_EINVALID;
    }

    sip->public.amp_gain = (int)gain;
    irq_restore(old);
    return MAPLE_EOK;
}

int sip_set_sample_type(maple_device_t *dev, unsigned int type) {
    sip_driver_state_t *sip;
    irq_mask_t old;

    if(type > SIP_SAMPLE_8BIT_ULAW)
        return MAPLE_EINVALID;

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        return MAPLE_EINVALID;
    }

    if(sip->public.is_sampling || sip->pending != SIP_COMMAND_NONE) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    sip->public.sample_type = (int)type;

    if(sip->capture) {
        sip->capture->sample_size = type == SIP_SAMPLE_16BIT_SIGNED ? 2u : 1u;
        sip_reset_capture_locked(sip->capture);
    }

    irq_restore(old);
    return MAPLE_EOK;
}

int sip_set_frequency(maple_device_t *dev, unsigned int frequency) {
    sip_driver_state_t *sip;
    irq_mask_t old;

    if(frequency > SIP_SAMPLE_8KHZ)
        return MAPLE_EINVALID;

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        return MAPLE_EINVALID;
    }

    if(sip->public.is_sampling || sip->pending != SIP_COMMAND_NONE) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    sip->public.frequency = (int)frequency;
    irq_restore(old);
    return MAPLE_EOK;
}

int sip_get_status(maple_device_t *dev, sip_device_status_t *status) {
    sip_driver_state_t *sip;
    irq_mask_t old;

    if(!status) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        errno = ENODEV;
        return -1;
    }

    status->amp_gain = sip->public.amp_gain;
    status->sample_type = sip->public.sample_type;
    status->frequency = sip->public.frequency;
    status->state = sip_device_capture_state(sip);
    status->last_result = sip->command_result;
    status->last_response = sip->last_response;
    status->sequence = sip->sequence;
    status->packets_received = sip->packets_received;
    status->samples_received = sip->samples_received;
    status->malformed_responses = sip->malformed_responses;
    status->command_failures = sip->command_failures;
    status->command_timeouts = sip->command_timeouts;
    status->sample_status = sip->sample_status;
    irq_restore(old);
    return 0;
}

int sip_start_sampling(maple_device_t *dev, sip_sample_cb callback, bool block) {
    if(!callback)
        return MAPLE_EFAIL;

    return sip_submit_control(dev, callback, SIP_COMMAND_START, block);
}

int sip_stop_sampling(maple_device_t *dev, bool block) {
    return sip_submit_control(dev, NULL, SIP_COMMAND_STOP, block);
}

static void sip_record_packet(maple_device_t *dev, sip_driver_state_t *sip,
                              const uint8_t *samples, size_t bytes,
                              uint32_t sample_status) {
    sip_capture_t *capture = sip->capture;
    size_t sample_size = sip->public.sample_type ==
        SIP_SAMPLE_16BIT_SIGNED ? 2u : 1u;

    /* A partial 16-bit sample is unusable and must never make the ring or
       sample counters lose alignment. Preserve the complete prefix. */
    if(bytes % sample_size) {
        bytes -= bytes % sample_size;
        ++sip->malformed_responses;
    }

    sip->sample_status = sample_status;

    if(bytes) {
        ++sip->packets_received;
        sip->samples_received += bytes / sample_size;
    }

    if(capture && bytes) {
        _sip_ring_write(capture->buffer, capture->buffer_size,
                        capture->write_bytes, samples, bytes);
        capture->write_bytes += bytes;
        ++capture->packets_received;
        capture->samples_received += bytes / sample_size;
        capture->sample_status = sample_status;
    }

    if(++sip->sequence == 0)
        ++sip->sequence;

    if(bytes && sip->public.callback)
        sip->public.callback(dev, (uint8_t *)samples, bytes);
}

static void sip_reply(maple_state_t *st, maple_frame_t *frame) {
    maple_response_t *response;
    sip_driver_state_t *sip;
    uint32_t *words;
    size_t bytes;
    bool recorded = false;

    (void)st;

    response = (maple_response_t *)frame->recv_buf;
    sip = frame->dev && frame->dev->status
        ? (sip_driver_state_t *)frame->dev->status : NULL;

    /* Response bytes remain stable until this IRQ callback returns. Publish
       or copy all useful data before making the per-device frame reusable. */
    if(sip && sip->public.is_sampling) {
        if(response->response != MAPLE_RESPONSE_DATATRF ||
           response->data_len < 2) {
            ++sip->malformed_responses;
        }
        else {
            words = (uint32_t *)response->data;

            if(words[0] != MAPLE_FUNC_MICROPHONE) {
                ++sip->malformed_responses;
            }
            else {
                bytes = ((size_t)response->data_len << 2) - 8u;
                sip_record_packet(frame->dev, sip, response->data + 8,
                                  bytes, words[1]);
                recorded = true;
            }
        }

        if(!recorded) {
            if(++sip->sequence == 0)
                ++sip->sequence;
        }
    }

    maple_frame_unlock(frame);
}

static int sip_poll(maple_device_t *dev) {
    sip_driver_state_t *sip = (sip_driver_state_t *)dev->status;

    if(!sip->public.is_sampling ||
       (!sip->public.callback && !sip->capture))
        return 0;

    if(maple_frame_trylock(&dev->frame) < 0)
        return 0;

    maple_frame_init(&dev->frame);
    dev->frame.send_buf[0] = MAPLE_FUNC_MICROPHONE;
    dev->frame.send_buf[1] = SIP_SUBCOMMAND_GET_SAMPLES |
                             (sip->public.amp_gain << 8);
    dev->frame.cmd = MAPLE_COMMAND_MICCONTROL;
    dev->frame.dst_port = dev->port;
    dev->frame.dst_unit = dev->unit;
    dev->frame.length = 2;
    dev->frame.callback = sip_reply;
    maple_queue_frame(&dev->frame);
    return 0;
}

static void sip_periodic(maple_driver_t *driver) {
    maple_driver_foreach(driver, sip_poll);
}

static int sip_attach(maple_driver_t *driver, maple_device_t *dev) {
    sip_driver_state_t *sip = (sip_driver_state_t *)dev->status;

    (void)driver;

    memset(sip, 0, sizeof(*sip));
    sip->public.amp_gain = SIP_DEFAULT_GAIN;
    sip->public.sample_type = SIP_SAMPLE_16BIT_SIGNED;
    sip->public.frequency = SIP_SAMPLE_11KHZ;
    sip->command_result = MAPLE_EOK;
    sip->last_response = MAPLE_RESPONSE_NONE;
    return 0;
}

static void sip_detach(maple_driver_t *driver, maple_device_t *dev) {
    sip_driver_state_t *sip = (sip_driver_state_t *)dev->status;

    (void)driver;

    if(sip->capture) {
        sip->capture->dev = NULL;
        sip->capture->state = SIP_CAPTURE_DISCONNECTED;
        sip->capture = NULL;
    }

    sip->public.is_sampling = false;
    sip->public.callback = NULL;
    sip->pending = SIP_COMMAND_NONE;
}

sip_capture_t *sip_capture_create(maple_device_t *dev, void *buffer,
                                  size_t buffer_size) {
    sip_driver_state_t *sip;
    sip_capture_t *capture;
    irq_mask_t old;
    size_t sample_size;

    if(!buffer || !buffer_size || (buffer_size & 1u)) {
        errno = EINVAL;
        return NULL;
    }

    capture = calloc(1, sizeof(*capture));
    if(!capture)
        return NULL;

    old = irq_disable();
    sip = sip_state_private(dev);

    if(!sip) {
        irq_restore(old);
        free(capture);
        errno = ENODEV;
        return NULL;
    }

    if(sip->capture || sip->public.is_sampling ||
       sip->pending != SIP_COMMAND_NONE) {
        irq_restore(old);
        free(capture);
        errno = EBUSY;
        return NULL;
    }

    sample_size = sip->public.sample_type == SIP_SAMPLE_16BIT_SIGNED ? 2u : 1u;

    if(buffer_size % sample_size) {
        irq_restore(old);
        free(capture);
        errno = EINVAL;
        return NULL;
    }

    capture->dev = dev;
    capture->buffer = buffer;
    capture->buffer_size = buffer_size;
    capture->sample_size = sample_size;
    capture->state = SIP_CAPTURE_STOPPED;
    sip->capture = capture;
    irq_restore(old);
    return capture;
}

int sip_capture_destroy(sip_capture_t *capture) {
    sip_driver_state_t *sip;
    irq_mask_t old;

    if(!capture) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();

    if(capture->stream_count || capture->state == SIP_CAPTURE_STARTING ||
       capture->state == SIP_CAPTURE_RECORDING ||
       capture->state == SIP_CAPTURE_STOPPING) {
        irq_restore(old);
        errno = EBUSY;
        return -1;
    }

    if(capture->dev && capture->dev->status) {
        sip = (sip_driver_state_t *)capture->dev->status;

        if(sip->capture == capture)
            sip->capture = NULL;
    }

    capture->dev = NULL;
    irq_restore(old);
    free(capture);
    return 0;
}

int sip_capture_start(sip_capture_t *capture, bool block) {
    maple_device_t *dev;
    irq_mask_t old;

    if(!capture)
        return MAPLE_EINVALID;

    old = irq_disable();
    dev = capture->dev;

    if(!dev || capture->state == SIP_CAPTURE_DISCONNECTED) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    if(capture->state == SIP_CAPTURE_STARTING ||
       capture->state == SIP_CAPTURE_RECORDING ||
       capture->state == SIP_CAPTURE_STOPPING) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    irq_restore(old);
    return sip_submit_control(dev, NULL, SIP_COMMAND_START, block);
}

int sip_capture_stop(sip_capture_t *capture, bool block) {
    maple_device_t *dev;
    irq_mask_t old;

    if(!capture)
        return MAPLE_EINVALID;

    old = irq_disable();
    dev = capture->dev;

    if(!dev || capture->state == SIP_CAPTURE_DISCONNECTED) {
        irq_restore(old);
        return MAPLE_EFAIL;
    }

    irq_restore(old);
    return sip_submit_control(dev, NULL, SIP_COMMAND_STOP, block);
}

int sip_capture_get_status(sip_capture_t *capture,
                           sip_capture_status_t *status) {
    irq_mask_t old;
    uint64_t retained;

    if(!capture || !status) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    retained = capture->write_bytes;

    if(retained > capture->buffer_size)
        retained = capture->buffer_size;

    status->state = capture->state;
    status->buffer_size = capture->buffer_size;
    status->sample_size = capture->sample_size;
    status->buffered_samples = (size_t)retained / capture->sample_size;
    status->write_position = capture->write_bytes / capture->sample_size;
    status->packets_received = capture->packets_received;
    status->samples_received = capture->samples_received;
    status->sample_status = capture->sample_status;
    irq_restore(old);
    return 0;
}

sip_stream_t *sip_stream_open(sip_capture_t *capture, bool from_latest) {
    sip_stream_t *stream;
    irq_mask_t old;

    if(!capture) {
        errno = EINVAL;
        return NULL;
    }

    stream = calloc(1, sizeof(*stream));
    if(!stream)
        return NULL;

    old = irq_disable();
    stream->capture = capture;
    stream->read_bytes = from_latest ? capture->write_bytes
        : _sip_ring_oldest(capture->write_bytes, capture->buffer_size);
    stream->next = capture->streams;
    capture->streams = stream;
    ++capture->stream_count;
    irq_restore(old);
    return stream;
}

int sip_stream_close(sip_stream_t *stream) {
    sip_capture_t *capture;
    sip_stream_t **link;
    irq_mask_t old;

    if(!stream || !stream->capture) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    capture = stream->capture;
    link = &capture->streams;

    while(*link && *link != stream)
        link = &(*link)->next;

    if(!*link) {
        irq_restore(old);
        errno = EINVAL;
        return -1;
    }

    *link = stream->next;
    --capture->stream_count;
    stream->capture = NULL;
    irq_restore(old);
    free(stream);
    return 0;
}

static size_t sip_stream_available_locked(sip_stream_t *stream) {
    sip_capture_t *capture = stream->capture;
    uint64_t lost = 0;
    size_t bytes;

    bytes = _sip_ring_available(capture->write_bytes, capture->buffer_size,
                                &stream->read_bytes, &lost);

    if(lost) {
        stream->lost_bytes += lost;
        stream->overrun = true;
    }

    return bytes;
}

int sip_stream_get_status(sip_stream_t *stream,
                          sip_stream_status_t *status) {
    sip_capture_t *capture;
    irq_mask_t old;
    size_t available;

    if(!stream || !stream->capture || !status) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    capture = stream->capture;
    available = sip_stream_available_locked(stream);
    status->read_position = stream->read_bytes / capture->sample_size;
    status->available_samples = available / capture->sample_size;
    status->lost_samples = stream->lost_bytes / capture->sample_size;
    status->overrun = stream->overrun;
    status->disconnected = capture->state == SIP_CAPTURE_DISCONNECTED;
    irq_restore(old);
    return 0;
}

ssize_t sip_stream_read(sip_stream_t *stream, void *buffer, size_t samples) {
    sip_capture_t *capture;
    uint8_t *output = buffer;
    irq_mask_t old;
    size_t requested_bytes;
    size_t copied = 0;
    size_t available;
    size_t chunk;
    size_t sample_size;

    if(!stream || !stream->capture || (!buffer && samples)) {
        errno = EINVAL;
        return -1;
    }

    capture = stream->capture;
    sample_size = capture->sample_size;

    if(samples > SIZE_MAX / sample_size) {
        errno = EOVERFLOW;
        return -1;
    }

    requested_bytes = samples * sample_size;

    /* Keep every interrupt-masked copy bounded. Between chunks, recomputing
       the oldest position detects a writer overtake without returning torn
       ring data. */
    while(copied < requested_bytes) {
        old = irq_disable();
        available = sip_stream_available_locked(stream);

        if(!available) {
            irq_restore(old);
            break;
        }

        chunk = requested_bytes - copied;
        if(chunk > available)
            chunk = available;
        if(chunk > SIP_COPY_CHUNK)
            chunk = SIP_COPY_CHUNK;

        chunk -= chunk % sample_size;
        _sip_ring_copy(capture->buffer, capture->buffer_size,
                       stream->read_bytes, output + copied, chunk);
        stream->read_bytes += chunk;
        irq_restore(old);
        copied += chunk;
    }

    return (ssize_t)(copied / sample_size);
}

int sip_stream_seek(sip_stream_t *stream, int64_t offset, int whence) {
    sip_capture_t *capture;
    irq_mask_t old;
    uint64_t oldest;
    uint64_t base;
    uint64_t magnitude;
    uint64_t byte_offset;
    uint64_t target;

    if(!stream || !stream->capture) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    capture = stream->capture;
    (void)sip_stream_available_locked(stream);
    oldest = _sip_ring_oldest(capture->write_bytes, capture->buffer_size);

    if(whence == SIP_STREAM_SEEK_OLDEST)
        base = oldest;
    else if(whence == SIP_STREAM_SEEK_CURRENT)
        base = stream->read_bytes;
    else if(whence == SIP_STREAM_SEEK_LATEST)
        base = capture->write_bytes;
    else {
        irq_restore(old);
        errno = EINVAL;
        return -1;
    }

    magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1u
                           : (uint64_t)offset;

    if(magnitude > UINT64_MAX / capture->sample_size) {
        irq_restore(old);
        errno = EOVERFLOW;
        return -1;
    }

    byte_offset = magnitude * capture->sample_size;

    if(offset < 0) {
        if(byte_offset > base) {
            irq_restore(old);
            errno = ERANGE;
            return -1;
        }

        target = base - byte_offset;
    }
    else {
        if(byte_offset > UINT64_MAX - base) {
            irq_restore(old);
            errno = EOVERFLOW;
            return -1;
        }

        target = base + byte_offset;
    }

    if(target < oldest || target > capture->write_bytes) {
        irq_restore(old);
        errno = ERANGE;
        return -1;
    }

    stream->read_bytes = target;
    irq_restore(old);
    return 0;
}

int sip_stream_clear_overrun(sip_stream_t *stream) {
    irq_mask_t old;

    if(!stream || !stream->capture) {
        errno = EINVAL;
        return -1;
    }

    old = irq_disable();
    stream->lost_bytes = 0;
    stream->overrun = false;
    irq_restore(old);
    return 0;
}

static maple_driver_t sip_driver = {
    .functions = MAPLE_FUNC_MICROPHONE,
    .name = "Sound Input Peripheral",
    .periodic = sip_periodic,
    .status_size = sizeof(sip_driver_state_t),
    .attach = sip_attach,
    .detach = sip_detach
};

void sip_init(void) {
    maple_driver_reg(&sip_driver);
}

void sip_shutdown(void) {
    maple_driver_unreg(&sip_driver);
}
