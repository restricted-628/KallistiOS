/* Private state candidate for one record in the existing Audio allocator.
   Caller serializes every operation under that allocator's owner lock.
   Persistent record identity and generations survive release and reuse. */
#ifndef SND_MEM_RESERVATION_H
#define SND_MEM_RESERVATION_H

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t owner;
    uint64_t writer;
    uint32_t base;
    size_t extent;
    size_t filled;
    size_t writing;
} wave_reservation_t;

typedef struct {
    uint64_t owner;
    uint64_t writer;
} wave_write_t;

/* Audio has allocated and verified the unpublished span under its lock.
   An owner token names this specific persistent record and its generation. */
static inline int wave_reserve(wave_reservation_t *r, uint32_t base,
                               size_t bytes, uint64_t *owner) {
    if(r->extent)
        return EBUSY;
    if(!base || !bytes || (base & 31u) || (bytes & 31u)
            || bytes > UINT32_MAX - base)
        return EINVAL;
    if(r->owner == UINT64_MAX)
        return EOVERFLOW;

    r->base = base;
    r->extent = bytes;
    r->filled = 0;
    r->writer = 0;
    *owner = ++r->owner;
    return 0;
}

static inline bool wave_live(const wave_reservation_t *r, uint64_t owner) {
    return r->extent && r->owner == owner;
}

/* Pin exactly the contiguous next part. Drop the Audio lock before G1. */
static inline int wave_begin(wave_reservation_t *r, uint64_t owner,
                             size_t offset, size_t bytes,
                             wave_write_t *write, uint32_t *destination) {
    if(!wave_live(r, owner))
        return ESTALE;
    if(r->writing)
        return EBUSY;
    if(!bytes || (bytes & 31u) || offset != r->filled
            || bytes > r->extent - r->filled)
        return EINVAL;
    if(r->writer == UINT64_MAX)
        return EOVERFLOW;

    r->writing = bytes;
    *write = (wave_write_t){owner, ++r->writer};
    *destination = r->base + (uint32_t)offset;
    return 0;
}

/* Only a returning, fully retired GD call can complete a writer. Partial
   failure leaves the prefix unchanged, so a retry must overwrite that area
   before the reservation can be adopted. */
static inline int wave_finish(wave_reservation_t *r, wave_write_t write,
                              int error, size_t written) {
    if(!wave_live(r, write.owner) || !r->writing || r->writer != write.writer)
        return ESTALE;
    if(!error && written != r->writing)
        error = EIO;
    if(!error)
        r->filled += r->writing;
    r->writing = 0;
    return error;
}

/* Audio either adopts the full live allocation or frees a discarded one
   under its own allocator protocol. Success invalidates the token. */
static inline int wave_close(wave_reservation_t *r, uint64_t owner,
                             bool adopt, uint32_t *base) {
    if(!wave_live(r, owner))
        return ESTALE;
    if(r->writing)
        return EBUSY;
    if(adopt && r->filled != r->extent)
        return ENODATA;

    *base = r->base;
    r->extent = 0;
    return 0;
}

int snd_mem_reconfigure_begin(void);
int snd_mem_reconfigure_end(void);
int snd_mem_reconfigure_require_owner(void);
int snd_mem_reconfigure_init(uint32_t reserve);
int snd_mem_reconfigure_shutdown(void);

int snd_mem_reserve_wave(size_t bytes, uint64_t *owner);
int snd_mem_wave_begin(uint64_t owner, size_t offset, size_t length,
                       wave_write_t *writer, uint32_t *destination);
int snd_mem_wave_finish(wave_write_t writer, int error, size_t transferred);
/* Null output discards the reservation and coalesces its block. */
int snd_mem_wave_close(uint64_t owner, uint32_t *published);

#endif
