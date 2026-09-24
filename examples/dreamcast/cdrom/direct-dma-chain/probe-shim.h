#ifndef DIRECT_DMA_CHAIN_PROBE_SHIM_H
#define DIRECT_DMA_CHAIN_PROBE_SHIM_H
#include <kos/timer.h>
#include <stdlib.h>
uint64_t chain_probe_time(void);
#define timer_ms_gettime64() chain_probe_time()
#ifdef CHAIN_ALLOC_SPY
void *chain_probe_malloc(size_t size);
void chain_probe_free(void *ptr);
#define malloc(size) chain_probe_malloc(size)
#define free(ptr) chain_probe_free(ptr)
#endif
#endif
