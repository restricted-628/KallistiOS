/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <arch/cache_range.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int checks;
#define CHECK(expr) do { ++checks; if(!(expr)) { \
    fprintf(stderr, "CACHE-RANGE: FAIL line=%d\n", __LINE__); exit(1); \
} } while(0)

static void range(uintptr_t start, size_t count) {
    uintptr_t first = 17, last = 19;
    /* Independent subtraction-based oracle: a range must fit in the
       remaining bytes of its 512 MiB area, including its final byte. */
    size_t remaining = (size_t)UINT32_C(0x20000000)
        - (size_t)(start % UINT32_C(0x20000000));
    bool valid = count != 0 && count <= remaining;
    CHECK(arch_cache_range(start, count, &first, &last) == valid);
    if(valid) {
        CHECK(first == (start / 32) * 32);
        CHECK(last == ((start + count - 1) / 32) * 32);
        CHECK(first <= last && last - first <= remaining);
    }
    else {
        CHECK(first == 17 && last == 19);
    }
}

int main(void) {
    static const size_t counts[] = { 0, 1, 2, 15, 31, 32, 33, 63, 64,
        65, 39935, 39936, 65559, 65560, 0x20000000, 0x20000001, SIZE_MAX };
    for(uint32_t area = 0; area < 8; ++area) {
        uintptr_t base = (uintptr_t)area << 29;
        for(uint32_t offset = 0; offset < 128; ++offset) {
            for(size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n) {
                range(base + offset, counts[n]);
                range(base + UINT32_C(0x1fffffff) - offset, counts[n]);
            }
            uintptr_t address = base + offset;
            uintptr_t expected = area == 5 ? UINT32_C(0x80000000) + offset : address;
            CHECK(arch_cacheable_alias(address) == expected);
        }
    }
    for(uintptr_t offset = 0; offset < 64; ++offset)
        for(size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n)
            range(UINTPTR_MAX - offset, counts[n]);
    puts("CACHE-RANGE: arithmetic only; cache hardware is not modeled");
    printf("CACHE-RANGE: PASS checks=%u\n", checks);
    return 0;
}
