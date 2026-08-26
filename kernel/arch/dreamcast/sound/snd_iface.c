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
#include <errno.h>

#include <kos/dbglog.h>
#include <kos/thread.h>
#include <kos/mutex.h>
#include <kos/timer.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>

#include "arm/aica_cmd_iface.h"

/* Include the default firmware blob */
#include "snd_stream_drv.c"

/* Are we initted? */
static int initted = 0;

/* The queue processing mutex for snd_sh4_to_aica_start and snd_sh4_to_aica_stop.
   There are some cases like stereo stream control + stereo sfx control
   at the same time in separate threads. */
static mutex_t queue_proc_mutex = RECURSIVE_MUTEX_INITIALIZER;

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
        errno = EPROTO;
        return -1;
    }

    *data_address = SPU_RAM_UNCACHED_BASE + data;
    *queue_size = size;
    *head = current_head;
    *tail = current_tail;
    return 0;
}

/* Initialize driver; note that this replaces the AICA program so that
   if you had anything else going on, it's gone now! */
int snd_init(void) {
    size_t amt;

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
    }

    initted = 1;

    return 0;
}

/* Shut everything down and free mem */
void snd_shutdown(void) {
    if(initted) {
        spu_disable();
        snd_mem_shutdown();
        initted = 0;
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

    g2_lock_scoped();

    qa = SPU_RAM_UNCACHED_BASE + AICA_MEM_RESP_QUEUE;

    if(queue_geometry(qa, AICA_MEM_CHANNELS, &bot, &queue_size,
                      &head, &tail) < 0)
        return -1;

    top = bot + queue_size;
    start = bot + tail;
    stop = bot + head;
    cnt = 0;
    pkt32 = (uint32_t *)packetout;

    /* Is there anything? */
    if(start == stop) {
        return 0;
    }

    /* Check for packet size overflow */
    size = g2_read_32_raw(start + offsetof(aica_cmd_t, size));

    if(size < sizeof(aica_cmd_t) / sizeof(uint32_t) ||
       size >= AICA_CMD_MAX_SIZE) {
        dbglog(DBG_ERROR, "snd_aica_to_sh4(): packet larger than %d dwords\n", AICA_CMD_MAX_SIZE);
        errno = EPROTO;
        return -1;
    }

    available = head >= tail ? head - tail : queue_size - (tail - head);

    if(size > available / sizeof(uint32_t)) {
        errno = EPROTO;
        return -1;
    }

    /* Find stop point for this packet */
    stop = start + size * 4;

    if(stop > top)
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

    return 1;
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
