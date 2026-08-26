/* KallistiOS ##version##

   main.c
   (c)2000-2002 Megan Potter
   Copyright (C) 2026 Joseph Black

   Generic sound driver with streaming capabilities

   This slightly more complicated version allows for sound effect channels,
   and full sampling rate, panning, and volume control for each.

*/

#include "aica_cmd_iface.h"
#include "aica.h"

/****************** Timer *******************************************/

#define timer (*((volatile uint32 *)AICA_MEM_CLOCK))

void timer_wait(uint32 jiffies) {
    uint32 fin = timer + jiffies;

    while(timer <= fin)
        ;
}

/****************** Tiny Libc ***************************************/

#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t count) {
    uint8 *dest8 = (uint8 *)dest;
    const uint8 *src8 = (const uint8 *)src;
    uint32 *dest32;
    const uint32 *src32;

    /* If both src and dest are 4-byte aligned */
    if(((uint32)dest & 3) == 0 && ((uint32)src & 3) == 0) {
        dest32 = (uint32 *)dest;
        src32 = (const uint32 *)src;

        /* Copy 4-byte chunks */
        while(count >= 4) {
            *dest32++ = *src32++;
            count -= 4;
        }

        /* Handle remaining bytes (if count was not divisible by 4) */
        dest8 = (uint8 *)dest32;
        src8 = (const uint8 *)src32;
    }

    /* Handle unaligned or remaining bytes */
    while(count--) {
        *dest8++ = *src8++;
    }

    return dest;
}

/****************** Main Program ************************************/

/* Our SH-4 interface (statically placed memory structures) */
volatile aica_queue_t   *q_cmd = (volatile aica_queue_t *)AICA_MEM_CMD_QUEUE;
volatile aica_queue_t   *q_resp = (volatile aica_queue_t *)AICA_MEM_RESP_QUEUE;
volatile aica_channel_t *chans = (volatile aica_channel_t *)AICA_MEM_CHANNELS;

static uint32 commands_processed;
static uint32 commands_rejected;
static uint32 malformed_packets;
static uint32 responses_dropped;

static uint32 queue_used(const volatile aica_queue_t *queue) {
    uint32 head = queue->head;
    uint32 tail = queue->tail;

    return head >= tail ? head - tail : queue->size - (tail - head);
}

/* Publish the packet body before advancing head. The SH-4 treats head as the
   ownership handoff and therefore cannot observe a partially written reply. */
static int queue_response(const aica_cmd_t *response) {
    const uint32 *source = (const uint32 *)response;
    uint32 head = q_resp->head;
    uint32 tail = q_resp->tail;
    uint32 used = queue_used(q_resp);
    uint32 free_space = q_resp->size - used - sizeof(uint32);
    uint32 words = response->size;
    uint32 i;

    if((head & 3) || (tail & 3) || (q_resp->size & 3) ||
            !q_resp->size || head >= q_resp->size || tail >= q_resp->size ||
            words < sizeof(aica_cmd_t) / sizeof(uint32) ||
            words >= AICA_CMD_MAX_SIZE ||
            words > free_space / sizeof(uint32)) {
        ++responses_dropped;
        return 0;
    }

    for(i = 0; i < words; ++i) {
        *((volatile uint32 *)(q_resp->data + head)) = source[i];
        head += sizeof(uint32);
        if(head >= q_resp->size)
            head = 0;
    }

    q_resp->head = head;
    return 1;
}

static void initialize_response(aica_cmd_t *response, uint32 words,
                                uint32 type, uint32 command_id) {
    uint32 i;

    response->size = words;
    response->cmd = type;
    response->timestamp = timer;
    response->cmd_id = command_id;
    for(i = 0; i < 4; ++i)
        response->misc[i] = 0;
}

static void respond_to_ping(uint32 command_id) {
    uint32 packet[sizeof(aica_cmd_t) / sizeof(uint32)];
    aica_cmd_t *response = (aica_cmd_t *)packet;

    initialize_response(response, sizeof(packet) / sizeof(packet[0]),
                        AICA_RESP_PONG, command_id);
    queue_response(response);
}

static void respond_with_driver_info(uint32 command_id) {
    AICA_CMDSTR_DRIVER_INFO(packet, response, info);

    initialize_response(response, AICA_CMDSTR_DRIVER_INFO_SIZE,
                        AICA_RESP_DRIVER_INFO, command_id);
    info->protocol_version = AICA_DRIVER_PROTOCOL_VERSION;
    info->firmware_version = AICA_DRIVER_FIRMWARE_VERSION;
    info->features = AICA_DRIVER_FEATURE_SYNC_CHANNELS |
                     AICA_DRIVER_FEATURE_VALIDATION |
                     AICA_DRIVER_FEATURE_POSITION;
    info->uptime_ms = timer;
    info->commands_processed = commands_processed;
    info->commands_rejected = commands_rejected;
    info->malformed_packets = malformed_packets;
    info->responses_dropped = responses_dropped;
    info->command_queue_size = q_cmd->size;
    info->command_queue_used = queue_used(q_cmd);
    info->response_queue_size = q_resp->size;
    info->response_queue_used = queue_used(q_resp);
    queue_response(response);
}

static int channel_start_valid(const aica_channel_t *channel) {
    uint32 sample_bytes;

    if(channel->type > AICA_SM_ADPCM_LS || !channel->freq ||
            channel->freq > ((uint32)0xffffffff >> 10) ||
            channel->vol > 255 || channel->pan > 255 ||
            channel->base >= AICA_RAM_END || !channel->length ||
            channel->length > 65534 ||
            channel->loopstart > channel->loopend ||
            channel->loopend > channel->length)
        return 0;

    if(channel->type == AICA_SM_16BIT)
        sample_bytes = channel->length * 2;
    else if(channel->type == AICA_SM_8BIT)
        sample_bytes = channel->length;
    else
        sample_bytes = (channel->length + 1) / 2;

    return sample_bytes <= AICA_RAM_END - channel->base;
}

/* Process a CHAN command */
int process_chn(uint32 chn, aica_channel_t *chndat) {
    switch(chndat->cmd & AICA_CH_CMD_MASK) {
        case AICA_CH_CMD_NONE:
            return 1;
        case AICA_CH_CMD_START:

            if(chndat->cmd & AICA_CH_START_SYNC) {
                /* Retain the original low-channel packet interpretation for
                   raw clients while the dedicated command covers all 64. */
                if(!chn)
                    return 0;
                aica_sync_play(chn, 0);
            }
            else if(channel_start_valid(chndat)) {
                memcpy((void*)(chans + chn), chndat, sizeof(aica_channel_t));
                chans[chn].pos = 0;
                aica_play(chn, chndat->cmd & AICA_CH_START_DELAY);
            }
            else {
                return 0;
            }

            return 1;
        case AICA_CH_CMD_STOP:
            aica_stop(chn);
            return 1;
        case AICA_CH_CMD_UPDATE:

            if(((chndat->cmd & AICA_CH_UPDATE_SET_FREQ) &&
                (!chndat->freq ||
                 chndat->freq > ((uint32)0xffffffff >> 10))) ||
               ((chndat->cmd & AICA_CH_UPDATE_SET_VOL) &&
                chndat->vol > 255) ||
               ((chndat->cmd & AICA_CH_UPDATE_SET_PAN) &&
                chndat->pan > 255))
                return 0;

            if(chndat->cmd & AICA_CH_UPDATE_SET_FREQ) {
                chans[chn].freq = chndat->freq;
                aica_freq(chn);
            }

            if(chndat->cmd & AICA_CH_UPDATE_SET_VOL) {
                chans[chn].vol = chndat->vol;
                aica_vol(chn);
            }

            if(chndat->cmd & AICA_CH_UPDATE_SET_PAN) {
                chans[chn].pan = chndat->pan;
                aica_pan(chn);
            }

            return 1;
        default:
            return 0;
    }
}

/* Process one packet of queue data */
uint32 process_one(uint32 tail) {
    uint32      pktdata[AICA_CMD_MAX_SIZE], *pdptr, size, i;
    volatile uint32 * src;
    aica_cmd_t  * pkt;

    src = (volatile uint32 *)(q_cmd->data + tail);
    pkt = (aica_cmd_t *)pktdata;
    pdptr = pktdata;

    /* Get the size field */
    size = *src;

    /* A zero, undersized, or oversized record has no trustworthy next packet
       boundary. The caller drops the remaining queue rather than spinning on
       the same malformed record forever. */
    if(size < sizeof(aica_cmd_t) / sizeof(uint32) ||
            size > AICA_CMD_MAX_SIZE) {
        ++malformed_packets;
        return 0;
    }

    /* Copy out the packet data */
    for(i = 0; i < size; i++) {
        *pdptr++ = *src++;

        if((uint32)src >= (q_cmd->data + q_cmd->size))
            src = (volatile uint32 *)q_cmd->data;
    }

    ++commands_processed;

    /* Figure out what type of packet it is */
    switch(pkt->cmd) {
        case AICA_CMD_NONE:
            if(size != sizeof(aica_cmd_t) / sizeof(uint32))
                ++commands_rejected;
            break;
        case AICA_CMD_PING:
            if(size == sizeof(aica_cmd_t) / sizeof(uint32))
                respond_to_ping(pkt->cmd_id);
            else
                ++commands_rejected;
            break;
        case AICA_CMD_CHAN:
            if(size == AICA_CMDSTR_CHANNEL_SIZE) {
                aica_channel_t *channel =
                    (aica_channel_t *)pkt->cmd_data;

                if(!((channel->cmd & AICA_CH_START_SYNC) ||
                     pkt->cmd_id < 64) ||
                   !process_chn(pkt->cmd_id, channel))
                    ++commands_rejected;
            }
            else
                ++commands_rejected;
            break;
        case AICA_CMD_SYNC_CLOCK:
            if(size == sizeof(aica_cmd_t) / sizeof(uint32))
                timer = 0;
            else
                ++commands_rejected;
            break;
        case AICA_CMD_SYNC_CHANNELS:
            if(size == AICA_CMDSTR_CHANNEL_MASK_SIZE) {
                aica_channel_mask_t *mask =
                    (aica_channel_mask_t *)pkt->cmd_data;
                if(mask->low || mask->high)
                    aica_sync_play(mask->low, mask->high);
                else
                    ++commands_rejected;
            }
            else
                ++commands_rejected;
            break;
        case AICA_CMD_QUERY_DRIVER:
            if(size == sizeof(aica_cmd_t) / sizeof(uint32))
                respond_with_driver_info(pkt->cmd_id);
            else
                ++commands_rejected;
            break;
        default:
            ++commands_rejected;
            break;
    }

    return size;
}

/* Look for an available request in the command queue; if one is there
   then process it and move the tail pointer. */
void process_cmd_queue(void) {
    uint32      head, tail, tsloc, ts;

    /* Grab these values up front in case SH-4 changes head */
    head = q_cmd->head;
    tail = q_cmd->tail;

    /* Do we have anything to process? */
    while(head != tail) {
        /* Look at the next packet. If our clock isn't there yet, then
           we won't process anything yet either. */
        tsloc = tail + offsetof(aica_cmd_t, timestamp);

        if(tsloc >= q_cmd->size)
            tsloc -= q_cmd->size;

        ts = *((volatile uint32*)(q_cmd->data + tsloc));

        if(ts > 0 && ts >= timer)
            return;

        /* Process it */
        ts = process_one(tail);

        if(!ts) {
            /* The record did not contain a usable size, so no later packet
               boundary can be recovered safely. Drop this queue snapshot. */
            q_cmd->tail = head;
            return;
        }

        /* Ok, skip over the packet */
        tail += ts * 4;

        if(tail >= q_cmd->size)
            tail -= q_cmd->size;

        q_cmd->tail = tail;
    }
}

int arm_main(void) {
    int i;

    commands_processed = 0;
    commands_rejected = 0;
    malformed_packets = 0;
    responses_dropped = 0;

    /* Setup our queues */
    q_cmd->head = q_cmd->tail = 0;
    q_cmd->data = AICA_MEM_CMD_QUEUE + sizeof(aica_queue_t);
    q_cmd->size = AICA_MEM_RESP_QUEUE - q_cmd->data;
    q_cmd->process_ok = 1;
    q_cmd->valid = 1;

    q_resp->head = q_resp->tail = 0;
    q_resp->data = AICA_MEM_RESP_QUEUE + sizeof(aica_queue_t);
    q_resp->size = AICA_MEM_CHANNELS - q_resp->data;
    q_resp->process_ok = 1;
    q_resp->valid = 1;

    /* Initialize the AICA part of the SPU */
    aica_init();

    /* Wait for a command */
    for(; ;) {
        /* Update channel position counters */
        for(i = 0; i < 64; i++)
            aica_get_pos(i);

        /* Check for a command */
        if(q_cmd->process_ok)
            process_cmd_queue();

        /* Little delay to prevent memory lock */
        timer_wait(10);
    }
}
