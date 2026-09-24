/* Production upload routine with a checked SQ/G2/AICA-memory model.
   This fixture does not emulate SH-4 instructions, caches, or bus timing. */
#include "shim.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Alignas(32) uint32_t source[8192];
static uint8_t ram[8u * 1024 * 1024];
static uint32_t queues[(16u * 1024 * 1024 + 4096) / 4];
size_t test_ram_size = 2u * 1024 * 1024;
static size_t checks, cases, writes, locks, batch_bytes, largest_batch;
static size_t fail_at;
static bool translated, interrupt_context, owned, pending;
static unsigned g2_depth;
static uintptr_t mapped;

#define CHECK(c) do { ++checks; if(!(c)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while(0)

bool irq_inside_int(void) { return interrupt_context; }
g2_ctx_t g2_lock(void) {
    CHECK(!g2_depth);
    g2_depth = 1;
    batch_bytes = 0;
    return 0;
}
void g2_unlock(g2_ctx_t ctx) {
    CHECK(g2_depth == 1 && ctx == 0 && !pending && !owned);
    CHECK(batch_bytes <= 4096);
    if(batch_bytes > largest_batch)
        largest_batch = batch_bytes;
    g2_depth = 0;
}
void g2_fifo_wait(void) { CHECK(g2_depth == 1); }
void g2_write_32_raw(uintptr_t address, uint32_t value) {
    CHECK(g2_depth == 1 && !pending);
    CHECK((address & ~MEM_AREA_CACHE_MASK) == MEM_AREA_P2_BASE);
    uintptr_t offset = (address & MEM_AREA_CACHE_MASK) - SPU_RAM_BASE;
    CHECK(offset + 4 <= test_ram_size);
    memcpy(ram + offset, &value, 4);
    batch_bytes += 4;
    ++writes;
}
uint32_t *sq_lock(void *destination) {
    CHECK(!g2_depth && !owned && !pending);
    if(++locks == fail_at) {
        errno = EOVERFLOW;
        return NULL;
    }
    owned = true;
    uintptr_t physical = (uintptr_t)destination & MEM_AREA_CACHE_MASK;
    mapped = translated ? physical & ~(uintptr_t)0xfffff : 0;
    return queues + (physical - mapped) / 4;
}
void sq_flush(void *queue) {
    CHECK(g2_depth == 1 && owned);
    uintptr_t offset = (uint8_t *)queue - (uint8_t *)queues;
    if(translated)
        CHECK(offset + 32 <= 0x200000);
    offset += mapped;
    CHECK(offset >= SPU_RAM_BASE && offset + 32 <= SPU_RAM_BASE + test_ram_size);
    memcpy(ram + (offset - SPU_RAM_BASE), queue, 32);
    pending = true;
    batch_bytes += 32;
    ++writes;
}
void sq_wait(void) { CHECK(owned && g2_depth == 1); pending = false; }
void sq_unlock(void) { CHECK(owned && !pending); owned = false; }

static void run_case(size_t bytes, uintptr_t left, uintptr_t right,
                     uintptr_t alias) {
    size_t channel = bytes / 2;
    memset(ram + left - 32, 0xa5, channel + 64);
    memset(ram + right - 32, 0xa5, channel + 64);
    errno = 0;
    uintptr_t base = alias == 1 ? SPU_RAM_BASE :
                     alias ? SPU_RAM_BASE | alias : 0;
    snd_pcm16_split_sq(source, base + left, base + right, bytes);
    CHECK(!errno && !g2_depth && !owned && !pending);
    for(size_t frame = 0; frame < bytes / 4; ++frame) {
        uint16_t l, r;
        memcpy(&l, ram + left + 2 * frame, 2);
        memcpy(&r, ram + right + 2 * frame, 2);
        CHECK(l == (source[frame] & 0xffffu));
        CHECK(r == (source[frame] >> 16));
    }
    for(size_t i = 0; i < 32; ++i) {
        CHECK(ram[left - 32 + i] == 0xa5 && ram[left + channel + i] == 0xa5);
        CHECK(ram[right - 32 + i] == 0xa5 && ram[right + channel + i] == 0xa5);
    }
    ++cases;
}

static void invalid(uint32_t *data, uintptr_t left, uintptr_t right,
                     size_t bytes, int error) {
    size_t prior = writes, prior_locks = locks;
    errno = 0;
    snd_pcm16_split_sq(data, left, right, bytes);
    CHECK(errno == error && writes == prior && locks == prior_locks);
    CHECK(!g2_depth && !owned && !pending);
}

int main(void) {
    for(size_t i = 0; i < sizeof(source) / sizeof(source[0]); ++i)
        source[i] = ((uint32_t)(uint16_t)(i * 997u + 0x8001u) << 16)
                  | (uint16_t)(i * 313u + 0x7fffu);
    for(unsigned mode = 0; mode < 2; ++mode) {
        translated = mode != 0;
        for(size_t bytes = 32; bytes <= 16416; bytes += 32) {
            run_case(bytes, 0xfffe0, 0x17ffe0, 0);
            run_case(bytes, 0x17ffe0, 0xfffe0, MEM_AREA_P2_BASE);
        }
        run_case(160, 0x20000, 0x30000, MEM_AREA_P1_BASE);
        run_case(96, 0x20000, 0x30000, 1);
        test_ram_size = 8u * 1024 * 1024;
        run_case(16416, 0x600020, 0x100020, 0);
        test_ram_size = 2u * 1024 * 1024;
    }
    invalid(NULL, 0x20000, 0x30000, 32, EINVAL);
    invalid(source + 1, 0x20000, 0x30000, 32, EINVAL);
    invalid(source, 0x20001, 0x30000, 32, EINVAL);
    invalid(source, 0x20000, 0x30000, 33, EINVAL);
    invalid(source, 0x1fffe0, 0x30000, 128, EINVAL);
    invalid(source, 0x20000, 0x1fffe0, 128, EINVAL);
    invalid(source, 0x20000, 0x20020, 128, EINVAL);
    invalid(source, 0xc0820000, 0x30000, 32, EINVAL);
    invalid((uint32_t *)(UINTPTR_MAX - 31), 0x20000, 0x30000, 64, EINVAL);
    interrupt_context = true;
    invalid(source, 0x20000, 0x30000, 32, EPERM);
    interrupt_context = false;
    errno = 0;
    snd_pcm16_split_sq(NULL, 0, 0, 0);
    CHECK(errno == 0);
    for(size_t failure = 1; failure <= 4; ++failure) {
        size_t before = writes;
        locks = 0;
        fail_at = failure;
        errno = 0;
        snd_pcm16_split_sq(source, 0x20000, 0x30000, 16384);
        CHECK(errno == EOVERFLOW && locks == failure);
        CHECK(!g2_depth && !owned && !pending);
        CHECK(writes - before == (failure - 1) * (4096 / 32));
    }
    CHECK(largest_batch == 4096);
    printf("STEREO-SQ host: %zu cases, %zu checks passed\n", cases, checks);
    return 0;
}
