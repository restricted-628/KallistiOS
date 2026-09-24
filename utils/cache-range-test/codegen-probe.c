/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <kos/cache.h>
void range_invalidate(uintptr_t start, size_t count) {
    dcache_inval_range(start, count);
}
void range_writeback(uintptr_t start, size_t count) {
    dcache_wback_range(start, count);
}
void range_purge(uintptr_t start, size_t count) {
    dcache_purge_range(start, count);
}
