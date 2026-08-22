#ifndef __KOS_CACHE_H
#define __KOS_CACHE_H

#include <stddef.h>
#include <stdint.h>

void dcache_wback_range(uintptr_t address, size_t length);
void dcache_inval_range(uintptr_t address, size_t length);

#endif
