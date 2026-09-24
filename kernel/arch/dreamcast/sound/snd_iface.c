/* KallistiOS ##version##

   snd_iface.c
   Copyright (C) 2000-2002 Megan Potter
   Copyright (C) 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

   SH-4 support routines for accessing the AICA via the standard KOS driver
*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>

#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <kos/mutex.h>
#include <kos/timer.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>

#include "arm/aica_cmd_iface.h"
#include "snd_iface_internal.h"

/* Include the default firmware blob */
#include "snd_stream_drv.c"

/* Are we initted? */
static int initted = 0;

/* The queue processing mutex for snd_sh4_to_aica_start and snd_sh4_to_aica_stop.
   There are some cases like stereo stream control + stereo sfx control
   at the same time in separate threads. */
static mutex_t queue_proc_mutex = RECURSIVE_MUTEX_INITIALIZER;
static mutex_t response_mutex = RECURSIVE_MUTEX_INITIALIZER;
static uint32_t next_command_id = 1;
static uint32_t driver_features;

_Static_assert(SND_DRIVER_FEATURE_SYNC_CHANNELS ==
               AICA_DRIVER_FEATURE_SYNC_CHANNELS,
               "synchronized-channel capability ABI");
_Static_assert(SND_DRIVER_FEATURE_VALIDATION ==
               AICA_DRIVER_FEATURE_VALIDATION,
               "command-validation capability ABI");
_Static_assert(SND_DRIVER_FEATURE_POSITION == AICA_DRIVER_FEATURE_POSITION,
               "channel-position capability ABI");
_Static_assert(SND_DRIVER_FEATURE_CHANNEL_CONTROL ==
               AICA_DRIVER_FEATURE_CHANNEL_CONTROL,
               "checked channel-control capability ABI");
_Static_assert(SND_CHANNEL_START_DELAYED == AICA_CHANNEL_START_DELAYED,
               "checked channel start flag ABI");
_Static_assert(SND_CHANNEL_UPDATE_ALL == AICA_CHANNEL_UPDATE_ALL,
               "checked channel update mask ABI");
_Static_assert(SND_CHANNEL_UPDATE_FREQUENCY ==
               AICA_CHANNEL_UPDATE_FREQUENCY,
               "channel frequency update ABI");
_Static_assert(SND_CHANNEL_UPDATE_VOLUME == AICA_CHANNEL_UPDATE_VOLUME,
               "channel volume update ABI");
_Static_assert(SND_CHANNEL_UPDATE_PAN == AICA_CHANNEL_UPDATE_PAN,
               "channel pan update ABI");
_Static_assert(SND_CHANNEL_UPDATE_ENVELOPE == AICA_CHANNEL_UPDATE_ENVELOPE,
               "channel envelope update ABI");
_Static_assert(SND_CHANNEL_UPDATE_LFO == AICA_CHANNEL_UPDATE_LFO,
               "channel LFO update ABI");
_Static_assert(SND_CHANNEL_UPDATE_ROUTING == AICA_CHANNEL_UPDATE_ROUTING,
               "channel routing update ABI");
_Static_assert(SND_CHANNEL_UPDATE_FILTER == AICA_CHANNEL_UPDATE_FILTER,
               "channel filter update ABI");
_Static_assert(SND_CHANNEL_SAMPLE_PCM16 == AICA_SM_16BIT &&
               SND_CHANNEL_SAMPLE_PCM8 == AICA_SM_8BIT &&
               SND_CHANNEL_SAMPLE_ADPCM == AICA_SM_ADPCM &&
               SND_CHANNEL_SAMPLE_ADPCM_LOOP == AICA_SM_ADPCM_LS,
               "channel sample format ABI");

static int channel_fields_valid(const snd_channel_config_t *config,
                                uint32_t fields) {
    size_t i;

    if((fields & SND_CHANNEL_UPDATE_FREQUENCY) &&
       (!config->sample_rate || config->sample_rate > (UINT32_MAX >> 10)))
        return 0;
    if((fields & SND_CHANNEL_UPDATE_ENVELOPE) &&
       (config->envelope.attack_rate > 31 ||
        config->envelope.decay1_rate > 31 ||
        config->envelope.decay2_rate > 31 ||
        config->envelope.release_rate > 31 ||
        config->envelope.decay_level > 31 ||
        config->envelope.key_rate_scaling > 15))
        return 0;
    if((fields & SND_CHANNEL_UPDATE_LFO) &&
       (config->lfo.frequency > 31 || config->lfo.pitch_waveform > 3 ||
        config->lfo.pitch_depth > 7 ||
        config->lfo.amplitude_waveform > 3 ||
        config->lfo.amplitude_depth > 7))
        return 0;
    if((fields & SND_CHANNEL_UPDATE_ROUTING) &&
       (config->routing.effect_channel > 15 ||
        config->routing.effect_send > 15 ||
        config->routing.direct_level > 15))
        return 0;
    if(fields & SND_CHANNEL_UPDATE_FILTER) {
        if(config->filter.resonance > 31 ||
           config->filter.attack_rate > 31 ||
           config->filter.decay1_rate > 31 ||
           config->filter.decay2_rate > 31 ||
           config->filter.release_rate > 31)
            return 0;

        for(i = 0; i < 5; ++i)
            if(config->filter.level[i] > 0x1fff)
                return 0;
    }

    return 1;
}

static void channel_config_pack(aica_channel_config_t *destination,
                                const snd_channel_config_t *source) {
    size_t i;

    memset(destination, 0, sizeof(*destination));
    destination->base = source->sample_address;
    destination->type = source->format;
    destination->length = source->sample_count;
    destination->loop = source->loop_enabled;
    destination->loopstart = source->loop_start;
    destination->loopend = source->loop_end;
    destination->freq = source->sample_rate;
    destination->vol = source->volume;
    destination->pan = source->pan;
    destination->attack_rate = source->envelope.attack_rate;
    destination->decay1_rate = source->envelope.decay1_rate;
    destination->decay2_rate = source->envelope.decay2_rate;
    destination->release_rate = source->envelope.release_rate;
    destination->decay_level = source->envelope.decay_level;
    destination->key_rate_scaling = source->envelope.key_rate_scaling;
    destination->envelope_hold = source->envelope.hold;
    destination->envelope_loop_link = source->envelope.loop_link;
    destination->lfo_reset = source->lfo.reset_on_start;
    destination->lfo_frequency = source->lfo.frequency;
    destination->pitch_lfo_wave = source->lfo.pitch_waveform;
    destination->pitch_lfo_depth = source->lfo.pitch_depth;
    destination->amplitude_lfo_wave = source->lfo.amplitude_waveform;
    destination->amplitude_lfo_depth = source->lfo.amplitude_depth;
    destination->effect_channel = source->routing.effect_channel;
    destination->effect_send = source->routing.effect_send;
    destination->direct_level = source->routing.direct_level;
    destination->filter_enabled = source->filter.enabled;
    destination->filter_resonance = source->filter.resonance;
    for(i = 0; i < 5; ++i)
        destination->filter_level[i] = source->filter.level[i];
    destination->filter_attack_rate = source->filter.attack_rate;
    destination->filter_decay1_rate = source->filter.decay1_rate;
    destination->filter_decay2_rate = source->filter.decay2_rate;
    destination->filter_release_rate = source->filter.release_rate;
}

static void channel_config_unpack(snd_channel_config_t *destination,
                                  const aica_channel_config_t *source) {
    size_t i;

    memset(destination, 0, sizeof(*destination));
    destination->sample_address = source->base;
    destination->format = source->type;
    destination->sample_count = source->length;
    destination->loop_enabled = source->loop;
    destination->loop_start = source->loopstart;
    destination->loop_end = source->loopend;
    destination->sample_rate = source->freq;
    destination->volume = source->vol;
    destination->pan = source->pan;
    destination->envelope.attack_rate = source->attack_rate;
    destination->envelope.decay1_rate = source->decay1_rate;
    destination->envelope.decay2_rate = source->decay2_rate;
    destination->envelope.release_rate = source->release_rate;
    destination->envelope.decay_level = source->decay_level;
    destination->envelope.key_rate_scaling = source->key_rate_scaling;
    destination->envelope.hold = source->envelope_hold;
    destination->envelope.loop_link = source->envelope_loop_link;
    destination->lfo.reset_on_start = source->lfo_reset;
    destination->lfo.frequency = source->lfo_frequency;
    destination->lfo.pitch_waveform = source->pitch_lfo_wave;
    destination->lfo.pitch_depth = source->pitch_lfo_depth;
    destination->lfo.amplitude_waveform = source->amplitude_lfo_wave;
    destination->lfo.amplitude_depth = source->amplitude_lfo_depth;
    destination->routing.effect_channel = source->effect_channel;
    destination->routing.effect_send = source->effect_send;
    destination->routing.direct_level = source->direct_level;
    destination->filter.enabled = source->filter_enabled;
    destination->filter.resonance = source->filter_resonance;
    for(i = 0; i < 5; ++i)
        destination->filter.level[i] = source->filter_level[i];
    destination->filter.attack_rate = source->filter_attack_rate;
    destination->filter.decay1_rate = source->filter_decay1_rate;
    destination->filter.decay2_rate = source->filter_decay2_rate;
    destination->filter.release_rate = source->filter_release_rate;
}

/* Validate a shared queue before using offsets supplied by the ARM firmware.
   The data region must stay between its queue header and the next reserved
   AICA memory area. Head and tail are byte offsets within that region. */
static int queue_geometry(uint32_t queue_address, uint32_t region_end,
                          uint32_t *data_address, uint32_t *queue_size,
                          uint32_t *head, uint32_t *tail) {
    uint32_t data;
    uint32_t size;
    uint32_t current_head;
    uint32_t current_tail;

    if(!g2_read_32_raw(queue_address + offsetof(aica_queue_t, valid))) {
        errno = ENODEV;
        return -1;
    }

    data = g2_read_32_raw(queue_address + offsetof(aica_queue_t, data));
    size = g2_read_32_raw(queue_address + offsetof(aica_queue_t, size));
    current_head = g2_read_32_raw(queue_address + offsetof(aica_queue_t, head));
    current_tail = g2_read_32_raw(queue_address + offsetof(aica_queue_t, tail));

    if((data & 3) || (size & 3) || (current_head & 3) ||
       (current_tail & 3) || !size ||
       data < queue_address - SPU_RAM_UNCACHED_BASE + sizeof(aica_queue_t) ||
       data >= region_end || size > region_end - data ||
       current_head >= size || current_tail >= size) {
        dbglog(DBG_ERROR,
               "snd: invalid queue at %08" PRIx32
               " data=%08" PRIx32 " size=%" PRIu32
               " head=%" PRIu32 " tail=%" PRIu32 "\n",
               queue_address, data, size, current_head, current_tail);
        errno = EPROTO;
        return -1;
    }

    *data_address = SPU_RAM_UNCACHED_BASE + data;
    *queue_size = size;
    *head = current_head;
    *tail = current_tail;
    return 0;
}

static int shared_queue_state(uint32_t queue_address, uint32_t region_end,
                              bool *empty, bool *processing) {
    uint32_t data_address;
    uint32_t queue_size;
    uint32_t head;
    uint32_t tail;

    g2_lock_scoped();

    if(queue_geometry(queue_address, region_end, &data_address, &queue_size,
                      &head, &tail) < 0)
        return -1;

    *empty = head == tail;
    if(processing) {
        *processing = g2_read_32_raw(
            queue_address + offsetof(aica_queue_t, process_ok)) != 0;
    }

    return 0;
}

bool snd_iface_is_initialized(void) {
    return initted != 0;
}

int snd_iface_exclusive_begin(uint32_t timeout_ms) {
    uint64_t deadline;
    bool empty;
    bool processing;

    if(!timeout_ms) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    deadline = timer_ms_gettime64() + timeout_ms;
    if(mutex_lock_timed(&queue_proc_mutex, timeout_ms) < 0)
        return -1;

    if(!initted) {
        errno = ENODEV;
        goto fail;
    }

    for(;;) {
        if(shared_queue_state(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE,
                              AICA_MEM_RESP_QUEUE, &empty, &processing) < 0)
            goto fail;
        if(!processing) {
            errno = EBUSY;
            goto fail;
        }
        if(empty)
            break;
        if(timer_ms_gettime64() >= deadline) {
            errno = ETIMEDOUT;
            goto fail;
        }
        thd_pass();
    }

    /* No producer can enter snd_sh4_to_aica() while this recursive mutex is
       held. Emptying the queue first means the ARM has finished the last
       admitted command before processing is paused. */
    g2_write_32(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE +
                offsetof(aica_queue_t, process_ok), 0);
    return 0;

fail:
    mutex_unlock(&queue_proc_mutex);
    return -1;
}

void snd_iface_exclusive_end(void) {
    g2_write_32(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE +
                offsetof(aica_queue_t, process_ok), 1);
    mutex_unlock(&queue_proc_mutex);
}

static int response_queue_peek_type(uint32_t *type) {
    uint32_t data_address;
    uint32_t queue_size;
    uint32_t head;
    uint32_t tail;
    uint32_t command_offset;

    g2_lock_scoped();

    if(queue_geometry(SPU_RAM_UNCACHED_BASE + AICA_MEM_RESP_QUEUE,
                      AICA_MEM_CHANNELS, &data_address, &queue_size,
                      &head, &tail) < 0)
        return -1;

    if(head == tail)
        return 0;

    command_offset = tail + offsetof(aica_cmd_t, cmd);
    if(command_offset >= queue_size)
        command_offset -= queue_size;

    *type = g2_read_32_raw(data_address + command_offset);
    return 1;
}

static uint32_t remaining_timeout(uint64_t deadline) {
    uint64_t now = timer_ms_gettime64();

    if(now >= deadline)
        return 0;

    return (uint32_t)(deadline - now);
}

/* Initialize driver; note that this replaces the AICA program so that
   if you had anything else going on, it's gone now! */
int snd_init(void) {
    size_t amt;
    snd_driver_status_t status;
    int status_result;

    /* Finish loading the stream driver */
    if(!initted) {
        spu_disable();
        spu_memset_sq(0, 0, AICA_RAM_START);
        amt = snd_stream_drv_size;

        if(amt % 4)
            amt = (amt + 4) & ~3;

        dbglog(DBG_DEBUG, "snd_init(): loading %zu bytes into SPU RAM\n", amt);
        spu_memload_sq(0, (void *)snd_stream_drv_data, amt);

        /* Enable the AICA and give it a few ms to start up */
        spu_enable();
        thd_sleep(10);

        /* Initialize the RAM allocator. Do not publish a usable driver when
           its sample-memory pool could not be created. */
        if(snd_mem_init(AICA_RAM_START) < 0) {
            spu_disable();
            return -1;
        }

        initted = 1;
        driver_features = 0;
        status_result = snd_driver_get_status(&status, 100);
        if(status_result < 0 ||
           !(status.features & SND_DRIVER_FEATURE_SYNC_CHANNELS)) {
            int saved_errno = status_result < 0 ? errno : EPROTO;

            if(status_result < 0) {
                dbglog(DBG_ERROR,
                       "snd_init(): firmware status query failed: %s\n",
                       strerror(saved_errno));
            }
            else {
                dbglog(DBG_ERROR,
                       "snd_init(): firmware feature mask %08" PRIx32
                       " lacks synchronized channel start\n",
                       status.features);
            }

            snd_shutdown();
            errno = saved_errno;
            return -1;
        }

        snd_dsp_system_reset();
    }

    initted = 1;

    return 0;
}

/* Shut everything down and free mem */
void snd_shutdown(void) {
    if(initted) {
        spu_disable();
        snd_dsp_system_shutdown();
        snd_mem_shutdown();
        initted = 0;
        driver_features = 0;
    }
}

/* Submit a request to the SH4->AICA queue; size is in uint32's */
int snd_sh4_to_aica(void *packet, uint32_t size) {
    uint32_t qa, bot, start, top, tail, queue_size, used, free_space;
    const uint32_t *pkt32;
    uint32_t cnt;

    if(!packet || size < sizeof(aica_cmd_t) / sizeof(uint32_t) ||
       size >= AICA_CMD_MAX_SIZE) {
        errno = EINVAL;
        return -1;
    }

    /* Recursive locking preserves the stop/submit/start batching interface:
       the batching thread owns one level while each submission takes another.
       Other threads cannot splice commands into that stopped batch. */
    if(mutex_lock_irqsafe(&queue_proc_mutex) < 0)
        return -1;

    g2_lock_scoped();

    qa = SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE;

    if(queue_geometry(qa, AICA_MEM_RESP_QUEUE, &bot, &queue_size,
                      &start, &tail) < 0) {
        mutex_unlock(&queue_proc_mutex);
        return -1;
    }

    used = start >= tail ? start - tail : queue_size - (tail - start);
    free_space = queue_size - used - sizeof(uint32_t);

    if(size > free_space / sizeof(uint32_t)) {
        errno = EAGAIN;
        mutex_unlock(&queue_proc_mutex);
        return -1;
    }

    top = bot + queue_size;
    start += bot;
    pkt32 = (const uint32_t *)packet;
    cnt = 0;

    while(size-- > 0) {
        /* Fifo wait if necessary */
        if((cnt++ & 7) == 0)
            g2_fifo_wait();

        /* Write the next dword */
        g2_write_32_raw(start, *pkt32++);

        /* Move our counters */
        start += 4;

        if(start >= top)
            start = bot;
    }

    /* Finally, write a new head value to signify that we've added
       a packet for it to process */
    if((cnt & 7) == 0)
        g2_fifo_wait();

    g2_write_32_raw(qa + offsetof(aica_queue_t, head), start - bot);

    /* We could wait until head == tail here for processing, but there's
       not really much point; it'll just slow things down. */
    mutex_unlock(&queue_proc_mutex);
    return 0;
}

/* Start processing requests in the queue */
void snd_sh4_to_aica_start(void) {
    g2_write_32(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE + offsetof(aica_queue_t, process_ok), 1);
    mutex_unlock(&queue_proc_mutex);
}

/* Stop processing requests in the queue */
void snd_sh4_to_aica_stop(void) {
    mutex_lock(&queue_proc_mutex);
    g2_write_32(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE + offsetof(aica_queue_t, process_ok), 0);
}

int snd_channels_start_sync(uint64_t channels) {
    AICA_CMDSTR_CHANNEL_MASK(tmp, cmd, mask);

    if(!initted) {
        errno = ENODEV;
        return -1;
    }

    if(!(driver_features & SND_DRIVER_FEATURE_SYNC_CHANNELS)) {
        errno = ENOTSUP;
        return -1;
    }

    if(!channels) {
        errno = EINVAL;
        return -1;
    }

    memset(tmp, 0, sizeof(tmp));
    cmd->size = AICA_CMDSTR_CHANNEL_MASK_SIZE;
    cmd->cmd = AICA_CMD_SYNC_CHANNELS;
    mask->low = (uint32_t)channels;
    mask->high = (uint32_t)(channels >> 32);
    return snd_sh4_to_aica(tmp, cmd->size);
}

int snd_channel_config_init(snd_channel_config_t *config) {
    size_t i;

    if(!config) {
        errno = EINVAL;
        return -1;
    }

    memset(config, 0, sizeof(*config));
    config->format = SND_CHANNEL_SAMPLE_PCM16;
    config->sample_rate = 44100;
    config->volume = 255;
    config->pan = 128;
    config->envelope.attack_rate = 31;
    config->envelope.release_rate = 31;
    config->envelope.key_rate_scaling = 15;
    config->routing.direct_level = 15;
    config->filter.resonance = 4;
    for(i = 0; i < 5; ++i)
        config->filter.level[i] = 0x1ff8;
    return 0;
}

int snd_channel_config_validate(const snd_channel_config_t *config) {
    uint32_t sample_bytes;

    if(!config || config->format < SND_CHANNEL_SAMPLE_PCM16 ||
       config->format > SND_CHANNEL_SAMPLE_ADPCM_LOOP ||
       config->sample_address < AICA_RAM_START ||
       config->sample_address >= AICA_RAM_END || !config->sample_count ||
       config->sample_count > 65534 ||
       config->loop_start >= config->loop_end ||
       config->loop_end > config->sample_count ||
       !channel_fields_valid(config, SND_CHANNEL_UPDATE_ALL)) {
        errno = EINVAL;
        return -1;
    }

    if(config->format == SND_CHANNEL_SAMPLE_PCM16)
        sample_bytes = config->sample_count * 2;
    else if(config->format == SND_CHANNEL_SAMPLE_PCM8)
        sample_bytes = config->sample_count;
    else
        sample_bytes = (config->sample_count + 1) / 2;

    if(sample_bytes > AICA_RAM_END - config->sample_address) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int channel_control_submit(unsigned int channel, uint32_t operation,
                                  uint32_t fields, uint32_t start_flags,
                                  const snd_channel_config_t *config) {
    AICA_CMDSTR_CHANNEL_CONTROL(packet, command, control);

    if(!initted) {
        errno = ENODEV;
        return -1;
    }
    if(!(driver_features & SND_DRIVER_FEATURE_CHANNEL_CONTROL)) {
        errno = ENOTSUP;
        return -1;
    }

    memset(packet, 0, sizeof(packet));
    command->size = AICA_CMDSTR_CHANNEL_CONTROL_SIZE;
    command->cmd = AICA_CMD_CHANNEL_CONTROL;
    command->cmd_id = channel;
    control->operation = operation;
    control->fields = fields;
    control->start_flags = start_flags;
    if(config)
        channel_config_pack(&control->config, config);
    return snd_sh4_to_aica(packet, command->size);
}

int snd_channel_start(unsigned int channel,
                      const snd_channel_config_t *config, uint32_t flags) {
    if(channel >= 64 || (flags & ~SND_CHANNEL_START_DELAYED) ||
       snd_channel_config_validate(config) < 0) {
        if(channel >= 64 || (flags & ~SND_CHANNEL_START_DELAYED))
            errno = EINVAL;
        return -1;
    }

    return channel_control_submit(channel, AICA_CHANNEL_OP_START, 0,
                                  flags, config);
}

int snd_channel_update(unsigned int channel,
                       const snd_channel_config_t *config, uint32_t fields) {
    if(channel >= 64 || !config || !fields ||
       (fields & ~SND_CHANNEL_UPDATE_ALL) ||
       !channel_fields_valid(config, fields)) {
        errno = EINVAL;
        return -1;
    }

    return channel_control_submit(channel, AICA_CHANNEL_OP_UPDATE, fields, 0,
                                  config);
}

int snd_channel_stop(unsigned int channel) {
    if(channel >= 64) {
        errno = EINVAL;
        return -1;
    }

    return channel_control_submit(channel, AICA_CHANNEL_OP_STOP, 0, 0, NULL);
}

int snd_channel_get_status_ex(unsigned int channel,
                              snd_channel_status_ex_t *status) {
    aica_channel_status_ext_t snapshot;
    uint32_t *words = (uint32_t *)&snapshot;
    uint32_t address;
    uint32_t first_sequence;
    uint32_t last_sequence;
    size_t word;
    unsigned int attempt;

    if(channel >= 64 || !status) {
        errno = EINVAL;
        return -1;
    }

    memset(status, 0, sizeof(*status));
    if(!initted) {
        errno = ENODEV;
        return -1;
    }
    if(!(driver_features & SND_DRIVER_FEATURE_CHANNEL_CONTROL)) {
        errno = ENOTSUP;
        return -1;
    }

    address = SPU_RAM_UNCACHED_BASE + AICA_CHANNEL_STATUS(channel);
    g2_lock_scoped();

    for(attempt = 0; attempt < 8; ++attempt) {
        g2_fifo_wait();
        first_sequence = g2_read_32_raw(address);
        if(first_sequence & 1)
            continue;

        words[0] = first_sequence;
        for(word = 1; word < sizeof(snapshot) / sizeof(words[0]); ++word) {
            if((word & 7) == 0)
                g2_fifo_wait();
            words[word] = g2_read_32_raw(address + word * sizeof(words[0]));
        }
        g2_fifo_wait();
        last_sequence = g2_read_32_raw(address);

        if(first_sequence == last_sequence && !(last_sequence & 1)) {
            status->sequence = last_sequence;
            status->configured = snapshot.configured != 0;
            status->playing = snapshot.playing != 0;
            status->position = snapshot.position & 0xffff;
            channel_config_unpack(&status->config, &snapshot.config);
            return 0;
        }
    }

    errno = EAGAIN;
    return -1;
}

/* Transfer one packet of data from the AICA->SH4 queue. Expects to
   find AICA_CMD_MAX_SIZE dwords of space available. Returns -1
   if failure, 0 for no packets available, 1 otherwise. Failure
   might mean a permanent failure since the queue is probably out of sync. */
int snd_aica_to_sh4(void *packetout) {
    uint32_t qa, bot, start, stop, top, size, cnt, *pkt32;
    uint32_t queue_size, head, tail, available;

    if(!packetout || ((uintptr_t)packetout & 3)) {
        errno = EINVAL;
        return -1;
    }

    if(mutex_lock_irqsafe(&response_mutex) < 0)
        return -1;

    g2_lock_scoped();

    qa = SPU_RAM_UNCACHED_BASE + AICA_MEM_RESP_QUEUE;

    if(queue_geometry(qa, AICA_MEM_CHANNELS, &bot, &queue_size,
                      &head, &tail) < 0) {
        mutex_unlock(&response_mutex);
        return -1;
    }

    top = bot + queue_size;
    start = bot + tail;
    stop = bot + head;
    cnt = 0;
    pkt32 = (uint32_t *)packetout;

    /* Is there anything? */
    if(start == stop) {
        mutex_unlock(&response_mutex);
        return 0;
    }

    /* Check for packet size overflow */
    size = g2_read_32_raw(start + offsetof(aica_cmd_t, size));

    if(size < sizeof(aica_cmd_t) / sizeof(uint32_t) ||
       size >= AICA_CMD_MAX_SIZE) {
        dbglog(DBG_ERROR,
               "snd_aica_to_sh4(): invalid packet size %" PRIu32
               " dwords\n", size);
        errno = EPROTO;
        mutex_unlock(&response_mutex);
        return -1;
    }

    available = head >= tail ? head - tail : queue_size - (tail - head);

    if(size > available / sizeof(uint32_t)) {
        dbglog(DBG_ERROR,
               "snd_aica_to_sh4(): packet size %" PRIu32
               " exceeds available response words %" PRIu32 "\n",
               size, available / (uint32_t)sizeof(uint32_t));
        errno = EPROTO;
        mutex_unlock(&response_mutex);
        return -1;
    }

    /* Find stop point for this packet */
    stop = start + size * 4;

    /* A packet ending exactly at the physical queue end wraps to offset zero.
       Leaving stop at top would make the wrapped reader loop forever. */
    if(stop >= top)
        stop -= queue_size;

    while(start != stop) {
        /* Fifo wait if necessary */
        if((cnt++ & 7) == 0)
            g2_fifo_wait();

        /* Read the next dword */
        *pkt32++ = g2_read_32_raw(start);

        /* Move our counters */
        start += 4;

        if(start >= top)
            start = bot;
    }

    /* Finally, write a new tail value to signify that we've removed a packet */
    if((cnt & 7) == 0)
        g2_fifo_wait();

    g2_write_32_raw(qa + offsetof(aica_queue_t, tail), start - bot);

    mutex_unlock(&response_mutex);
    return 1;
}

int snd_driver_get_status(snd_driver_status_t *status, uint32_t timeout_ms) {
    uint32_t request_words[sizeof(aica_cmd_t) / sizeof(uint32_t)] = {0};
    uint32_t response_words[AICA_CMD_MAX_SIZE];
    aica_cmd_t *request = (aica_cmd_t *)request_words;
    aica_cmd_t *response = (aica_cmd_t *)response_words;
    uint64_t deadline;
    uint32_t command_id;
    uint32_t response_type;
    uint32_t remaining;
    bool queue_empty;
    bool queue_processing;
    int result = -1;

    if(!status || !timeout_ms) {
        errno = EINVAL;
        return -1;
    }

    memset(status, 0, sizeof(*status));

    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }

    if(!initted) {
        errno = ENODEV;
        return -1;
    }

    deadline = timer_ms_gettime64() + timeout_ms;

    if(mutex_lock_timed(&queue_proc_mutex, timeout_ms) < 0)
        return -1;

    remaining = remaining_timeout(deadline);
    if(!remaining) {
        errno = ETIMEDOUT;
        goto unlock_command_queue;
    }

    if(mutex_lock_timed(&response_mutex, remaining) < 0)
        goto unlock_command_queue;

    /* With both host-side queue locks held, wait for prior commands to drain.
       An empty response queue then guarantees the next reply belongs to this
       query; low-level response consumers cannot have packets stolen. */
    for(;;) {
        if(shared_queue_state(SPU_RAM_UNCACHED_BASE + AICA_MEM_CMD_QUEUE,
                              AICA_MEM_RESP_QUEUE, &queue_empty,
                              &queue_processing) < 0)
            goto unlock_response_queue;

        if(!queue_processing) {
            errno = EBUSY;
            goto unlock_response_queue;
        }

        if(queue_empty)
            break;

        if(!remaining_timeout(deadline)) {
            errno = ETIMEDOUT;
            goto unlock_response_queue;
        }

        thd_pass();
    }

    /* A timed-out status query can leave its late reply in the ring. Retire
       only those private replies. Any other response belongs to a low-level
       consumer and makes this query fail without consuming that packet. */
    for(;;) {
        int peek_result = response_queue_peek_type(&response_type);
        int drain_result;

        if(peek_result < 0)
            goto unlock_response_queue;
        if(!peek_result)
            break;
        if(response_type != AICA_RESP_DRIVER_INFO) {
            errno = EBUSY;
            goto unlock_response_queue;
        }
        drain_result = snd_aica_to_sh4(response_words);
        if(drain_result != 1) {
            if(!drain_result)
                errno = EPROTO;
            goto unlock_response_queue;
        }
    }

    command_id = next_command_id++;
    if(!next_command_id)
        next_command_id = 1;

    request->size = sizeof(request_words) / sizeof(request_words[0]);
    request->cmd = AICA_CMD_QUERY_DRIVER;
    request->cmd_id = command_id;

    if(snd_sh4_to_aica(request, request->size) < 0) {
        goto unlock_response_queue;
    }

    do {
        int receive_result = snd_aica_to_sh4(response_words);

        if(receive_result < 0)
            goto unlock_response_queue;

        if(receive_result > 0) {
            if(response->cmd == AICA_RESP_DRIVER_INFO &&
               response->cmd_id == command_id) {
                const aica_driver_info_t *info;

                if(response->size != AICA_CMDSTR_DRIVER_INFO_SIZE) {
                    dbglog(DBG_ERROR,
                           "snd_driver_get_status(): response size %" PRIu32
                           ", expected %zu\n", response->size,
                           (size_t)AICA_CMDSTR_DRIVER_INFO_SIZE);
                    errno = EPROTO;
                    goto unlock_response_queue;
                }

                info = (const aica_driver_info_t *)response->cmd_data;
                if(info->protocol_version != AICA_DRIVER_PROTOCOL_VERSION) {
                    dbglog(DBG_ERROR,
                           "snd_driver_get_status(): protocol %08" PRIx32
                           ", expected %08x\n", info->protocol_version,
                           AICA_DRIVER_PROTOCOL_VERSION);
                    errno = EPROTO;
                    goto unlock_response_queue;
                }

                status->protocol_version = info->protocol_version;
                status->firmware_version = info->firmware_version;
                status->features = info->features;
                status->uptime_ms = info->uptime_ms;
                status->commands_processed = info->commands_processed;
                status->commands_rejected = info->commands_rejected;
                status->malformed_packets = info->malformed_packets;
                status->responses_dropped = info->responses_dropped;
                status->command_queue_size = info->command_queue_size;
                status->command_queue_used = info->command_queue_used;
                status->response_queue_size = info->response_queue_size;
                status->response_queue_used = info->response_queue_used;
                driver_features = status->features;
                result = 0;
                goto unlock_response_queue;
            }

            dbglog(DBG_ERROR,
                   "snd_driver_get_status(): unexpected response cmd=%08"
                   PRIx32 " id=%08" PRIx32 " expected=%08" PRIx32 "\n",
                   response->cmd, response->cmd_id, command_id);
            errno = EPROTO;
            goto unlock_response_queue;
        }
        else {
            thd_pass();
        }
    } while(timer_ms_gettime64() < deadline);

    errno = ETIMEDOUT;

unlock_response_queue:
    mutex_unlock(&response_mutex);
unlock_command_queue:
    mutex_unlock(&queue_proc_mutex);
    return result;
}

/* Poll for responses from the AICA. We assume here that we're not
   running in an interrupt handler (thread perhaps, of whoever
   is using us). */
void snd_poll_resp(void) {
    int rv;
    uint32_t pkt[AICA_CMD_MAX_SIZE];
    aica_cmd_t *pktcmd;

    pktcmd = (aica_cmd_t *)pkt;

    while((rv = snd_aica_to_sh4(pkt)) > 0) {
        dbglog(DBG_DEBUG, "snd_poll_resp(): Received packet id %08lx, ts %08lx from AICA\n",
               pktcmd->cmd, pktcmd->timestamp);
    }

    if(rv < 0)
        dbglog(DBG_ERROR, "snd_poll_resp(): snd_aica_to_sh4 failed, giving up\n");
}

uint16_t snd_get_pos(unsigned int ch) {
    if(ch >= 64) {
        errno = EINVAL;
        return 0;
    }

    return g2_read_32(SPU_RAM_UNCACHED_BASE + AICA_CHANNEL(ch) + offsetof(aica_channel_t, pos)) & 0xffff;
}

bool snd_is_playing(unsigned int ch) {
    if(ch >= 64) {
        errno = EINVAL;
        return false;
    }

    return g2_read_32(MEM_AREA_P2_BASE + 0x00700000 + 0x80 * ch) & AICA_CHANNEL_KEYONB;
}

int snd_channel_get_status(unsigned int ch, snd_channel_status_t *status) {
    if(ch >= 64 || !status) {
        errno = EINVAL;
        return -1;
    }

    g2_lock_scoped();

    status->position = g2_read_32_raw(SPU_RAM_UNCACHED_BASE + AICA_CHANNEL(ch) +
                                      offsetof(aica_channel_t, pos)) & 0xffff;
    status->playing = !!(g2_read_32_raw(MEM_AREA_P2_BASE + 0x00700000 +
                                       0x80 * ch) & AICA_CHANNEL_KEYONB);
    return 0;
}
