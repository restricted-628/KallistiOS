/* KallistiOS ##version##

   Stereo PCM16 store-queue uploads.
   Copyright (C) 2023, 2024, 2025, 2026 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdint.h>
#include <kos/irq.h>
#include <dc/g2bus.h>
#include <dc/sq.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>

/* Bound both the IRQ-disabled interval and the working set. A batch fits
   inside sq_lock's two-page mapping even when starting near a page end. */
#define PCM16_SQ_BATCH_BYTES 4096u

static int pcm16_spu_offset(uintptr_t address, size_t ram_size,
                            uintptr_t *offset) {
    if(address < ram_size) {
        *offset = address;
        return 0;
    }

    uintptr_t area = address & ~(uintptr_t)MEM_AREA_CACHE_MASK;
    uintptr_t physical = address & MEM_AREA_CACHE_MASK;

    if((area != 0 && area != MEM_AREA_P1_BASE && area != MEM_AREA_P2_BASE)
       || physical < SPU_RAM_BASE || physical - SPU_RAM_BASE >= ram_size)
        return -1;

    *offset = physical - SPU_RAM_BASE;
    return 0;
}

/* Each source word holds one little-endian L/R frame. Preserve sample order
   and use unsigned shifts, including samples with their sign bit set. */
static inline uint32_t pcm16_pair(const uint32_t *source, unsigned shift) {
    return ((source[0] >> shift) & 0xffffu)
         | ((source[1] >> shift) << 16);
}

static int pcm16_sq_channel(const uint32_t *source, uintptr_t destination,
                            size_t bytes, unsigned shift) {
    size_t bulk = bytes & ~(size_t)31;
    uint32_t *queue = NULL;
    g2_ctx_t ctx;

    /* Acquire before masking interrupts. No caller-computed SQ mask is valid
       for all MMU modes, and the other channel may be in another mapping. */
    if(bulk) {
        queue = sq_lock((void *)destination);
        if(!queue)
            return -1;
    }

    ctx = g2_lock();
    g2_fifo_wait();

    for(size_t remaining = bulk; remaining; remaining -= 32) {
        for(unsigned i = 0; i < 8; ++i)
            queue[i] = pcm16_pair(source + 2 * i, shift);
        sq_flush(queue);
        queue += 8;
        source += 16;
    }

    if(bulk)
        sq_wait();

    if(bytes != bulk)
        g2_fifo_wait();

    /* A 32-byte stereo tail is 16 bytes per channel. Finish it with PIO,
       advancing by the channel byte count, not the interleaved byte count. */
    destination = (destination + bulk) | MEM_AREA_P2_BASE;
    for(size_t remaining = bytes - bulk; remaining; remaining -= 4) {
        g2_write_32_raw(destination, pcm16_pair(source, shift));
        destination += 4;
        source += 2;
    }

    if(bulk)
        sq_unlock();
    g2_unlock(ctx);
    return 0;
}

void snd_pcm16_split_sq(uint32_t *data, uintptr_t left, uintptr_t right,
                        size_t size) {
    size_t ram_size, channel_bytes;

    if(!size)
        return;
    if(irq_inside_int()) {
        errno = EPERM;
        return;
    }

    ram_size = SPU_RAM_SIZE;
    channel_bytes = size / 2;
    if(!data || ((uintptr_t)data & 31) || (size & 31)
       || (uintptr_t)data > UINTPTR_MAX - (size - 1)
       || pcm16_spu_offset(left, ram_size, &left) < 0
       || pcm16_spu_offset(right, ram_size, &right) < 0
       || (left & 31) || (right & 31)
       || channel_bytes > ram_size - left
       || channel_bytes > ram_size - right
       || (left < right + channel_bytes && right < left + channel_bytes)) {
        errno = EINVAL;
        return;
    }

    left += SPU_RAM_BASE;
    right += SPU_RAM_BASE;
    for(size_t offset = 0; offset < channel_bytes;) {
        size_t bytes = channel_bytes - offset;
        if(bytes > PCM16_SQ_BATCH_BYTES)
            bytes = PCM16_SQ_BATCH_BYTES;

        /* Two bounded passes over the same interleaved block need no staging
           allocation and never assume both channels share an SQ mapping. */
        const uint32_t *source = data + offset / 2;
        if(pcm16_sq_channel(source, left + offset, bytes, 0) < 0
           || pcm16_sq_channel(source, right + offset, bytes, 16) < 0)
            return;
        offset += bytes;
    }
}
