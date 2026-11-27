/* Exercise the actual public constructor with deterministic retired failures. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SPU_RAM_SIZE_2MB 0x200000u
#define AICA_RAM_START 0x30000u
typedef int gdrom_direct_sector_type_t;
typedef struct { size_t transferred; } gdrom_direct_result_t;
typedef struct { bool initialized; uint32_t pool_base, pool_size; } snd_mem_status_t;
static snd_mem_status_t pool = {true, AICA_RAM_START, SPU_RAM_SIZE_2MB - AICA_RAM_START};
static int refusal, alloc_error, transport_error;
static unsigned allocs, frees, transfers;
static bool owned, retired;
static uint32_t published;

static size_t dma_range_bytes(size_t sectors, int type) {
    return type || sectors > SIZE_MAX / 2048 ? 0 : sectors * 2048;
}
static int aica_read_args(uint32_t fad, size_t sectors, size_t bytes, uint32_t ms) {
    if(refusal || fad < 150 || !sectors || !bytes || !ms) {
        errno = refusal ? refusal : EINVAL;
        return -1;
    }
    return 0;
}
static int snd_mem_get_status(snd_mem_status_t *s) { *s = pool; return 0; }
static uint32_t snd_mem_malloc(size_t n) {
    assert(n == 17 * 2048 && !owned);
    ++allocs;
    if(alloc_error) { errno = alloc_error; return 0; }
    owned = true;
    return AICA_RAM_START;
}
static void snd_mem_free(uint32_t p) {
    assert(owned && retired && p == AICA_RAM_START);
    ++frees;
    owned = false;
    errno = ERANGE; /* Unwind must preserve the transport error. */
}
static int gdrom_direct_read_sectors_dma_aica(uint32_t p, uint32_t fad, size_t n,
        int type, uint32_t ms, gdrom_direct_result_t *r) {
    assert(owned && p == AICA_RAM_START && published == UINT32_MAX);
    assert(fad == 150 && n == 17 && !type && ms == 500);
    ++transfers;
    retired = true;
    if(r) r->transferred = transport_error ? 16 * 2048 : 17 * 2048;
    if(transport_error) { errno = transport_error; return -1; }
    return 0;
}
#include "gdrom_aica_load.inc"

int main(void) {
    gdrom_direct_result_t r = {1};
    published = UINT32_MAX;
    assert(gdrom_direct_load_aica(NULL, 150, 17, 0, 500, &r) < 0 && errno == EINVAL);
    assert(!r.transferred);
    assert(gdrom_direct_load_aica(&published, 150, 0, 0, 500, &r) < 0);
    assert(gdrom_direct_load_aica(&published, 150, SIZE_MAX, 0, 500, &r) < 0);
    assert(gdrom_direct_load_aica(&published, 150, 1024, 0, 500, &r) < 0 && errno == EFBIG);
    refusal = EPERM;
    assert(gdrom_direct_load_aica(&published, 150, 17, 0, 500, &r) < 0 && errno == EPERM);
    refusal = 0;
    --pool.pool_base;
    assert(gdrom_direct_load_aica(&published, 150, 17, 0, 500, &r) < 0 && errno == ENODEV);
    ++pool.pool_base;
    assert(!allocs && !frees && !transfers && published == UINT32_MAX);
    alloc_error = ENOMEM;
    assert(gdrom_direct_load_aica(&published, 150, 17, 0, 500, &r) < 0 && errno == ENOMEM);
    assert(!frees && !transfers && published == UINT32_MAX);
    alloc_error = 0;
    const int errors[] = { EBUSY, ETIMEDOUT, EIO };
    for(unsigned i = 0; i < 3; ++i) {
        transport_error = errors[i];
        retired = false;
        assert(gdrom_direct_load_aica(&published, 150, 17, 0, 500, &r) < 0);
        assert(errno == errors[i] && published == UINT32_MAX && !owned);
        assert(transfers == i + 1 && frees == i + 1 && r.transferred == 16 * 2048);
    }
    transport_error = 0;
    assert(!gdrom_direct_load_aica(&published, 150, 17, 0, 500, &r));
    assert(published == AICA_RAM_START && r.transferred == 17 * 2048 && owned && frees == 3);
    snd_mem_free(published);
    puts("GD owned loader: allocation, retirement, rollback and publication PASS");
}
