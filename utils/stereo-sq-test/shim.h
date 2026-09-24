#ifndef STEREO_SQ_TEST_SHIM_H
#define STEREO_SQ_TEST_SHIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MEM_AREA_CACHE_MASK 0x1fffffffu
#define MEM_AREA_P1_BASE 0x80000000u
#define MEM_AREA_P2_BASE 0xa0000000u
#define SPU_RAM_BASE 0x00800000u
extern size_t test_ram_size;
#define SPU_RAM_SIZE test_ram_size
typedef unsigned g2_ctx_t;
bool irq_inside_int(void);
g2_ctx_t g2_lock(void);
void g2_unlock(g2_ctx_t ctx);
void g2_fifo_wait(void);
void g2_write_32_raw(uintptr_t address, uint32_t value);
uint32_t *sq_lock(void *destination);
void sq_unlock(void);
void sq_wait(void);
void sq_flush(void *queue);
void snd_pcm16_split_sq(uint32_t *data, uintptr_t left, uintptr_t right,
                        size_t size);
#endif
