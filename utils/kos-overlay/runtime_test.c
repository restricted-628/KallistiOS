/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Compile the actual runtime with only SR read and external OS services shimmed.
   Never execute the loaded SH-4 bytes on the host.
*/
#include "test_os.h"
#include <kos/overlay.h>
#include "package.h"
#include "sha256.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static _Thread_local uint32_t test_sr;
static _Thread_local int lock_depth, in_irq;
uint32_t _arch_mem_top = 0x8d000000;
static uint8_t *image;
static size_t image_bytes, cursor, read_cap, fail_at, eof_at;
static int opens, closes, reads, cache_calls, loads, unloads, allocations;
static int open_error, stat_error, close_error, allocation_error, backend;
static bool oversized, nonregular, live_fd;
static int64_t size_delta;
static ovl_expected_t trusted;
static void (*read_observer)(void);

int mutex_init(mutex_t *m, unsigned kind) {
    assert(kind == MUTEX_TYPE_NORMAL);
    return pthread_mutex_init(m, NULL);
}
int mutex_destroy(mutex_t *m) { return pthread_mutex_destroy(m); }
int mutex_lock(mutex_t *m) {
    assert(!lock_depth);
    int result = pthread_mutex_lock(m);
    if(!result) ++lock_depth;
    return result;
}
int mutex_unlock(mutex_t *m) {
    assert(lock_depth == 1);
    --lock_depth;
    return pthread_mutex_unlock(m);
}
int irq_inside_int(void) { return in_irq; }
int fs_iso9660_get_backend(void) { assert(!lock_depth); return backend; }
file_t fs_open(const char *path, int mode) {
    assert(!lock_depth && !live_fd && mode == O_RDONLY);
    assert(!strcmp(path, "/cd/member.dco"));
    ++opens;
    if(open_error) { errno = open_error; return -1; }
    cursor = 0;
    live_fd = true;
    return 7;
}
int fs_fstat(file_t fd, struct stat *status) {
    assert(!lock_depth && live_fd && fd == 7);
    if(stat_error) { errno = stat_error; return -1; }
    memset(status, 0, sizeof(*status));
    status->st_size = (off_t)image_bytes + size_delta;
    status->st_mode = nonregular ? S_IFDIR : S_IFREG;
    return 0;
}
ssize_t fs_read(file_t fd, void *out, size_t bytes) {
    assert(!lock_depth && live_fd && fd == 7 && bytes <= OVL_STAGING_BYTES);
    ++reads;
    if(read_observer) read_observer();
    if(oversized) return (ssize_t)bytes + 1;
    if(cursor >= fail_at) {
        memset(out, 0xfe, bytes); /* Failing backend may have modified buffer. */
        errno = EIO;
        return -1;
    }
    if(cursor >= eof_at) return 0;
    if(bytes > read_cap) bytes = read_cap;
    if(bytes > image_bytes - cursor) bytes = image_bytes - cursor;
    if(bytes > fail_at - cursor) bytes = fail_at - cursor;
    if(bytes > eof_at - cursor) bytes = eof_at - cursor;
    memcpy(out, image + cursor, bytes);
    cursor += bytes;
    return (ssize_t)bytes;
}
int fs_close(file_t fd) {
    assert(!lock_depth && live_fd && fd == 7);
    live_fd = false;
    ++closes;
    if(close_error) { errno = close_error; return -1; }
    return 0;
}
void icache_sync_range(uintptr_t base, size_t bytes) {
    assert(!lock_depth && !live_fd && base == 0x8c100000 && bytes == 8192);
    ++cache_calls;
}
static void *checked_calloc(size_t n, size_t size) {
    assert(!lock_depth);
    ++allocations;
    if(allocation_error) { errno = ENOMEM; return NULL; }
    return calloc(n, size);
}
static void *checked_aligned_alloc(size_t alignment, size_t bytes) {
    assert(!lock_depth && bytes == OVL_STAGING_BYTES && alignment == 32);
    ++allocations;
    if(allocation_error) { errno = ENOMEM; return NULL; }
    void *p = NULL;
    assert(!posix_memalign(&p, alignment, bytes));
    return p;
}
static void checked_free(void *p) { assert(!lock_depth); free(p); }
#define calloc checked_calloc
#define aligned_alloc checked_aligned_alloc
#define free checked_free
#include "overlay-under-test.c"
#undef calloc
#undef aligned_alloc
#undef free

static void reset_io(void) {
    assert(!live_fd && !lock_depth);
    opens = closes = reads = cache_calls = loads = unloads = allocations = 0;
    open_error = stat_error = close_error = allocation_error = 0;
    oversized = nonregular = false;
    size_delta = 0;
    read_cap = fail_at = eof_at = SIZE_MAX;
    read_observer = NULL;
    backend = FS_ISO9660_BACKEND_DIRECT;
}

static void busy_checks(ovl_slot_t *slot) {
    ovl_token_t token = {0};
    ovl_view_t view;
    memset(&view, 0xa7, sizeof(view));
    ovl_view_t before = view;
    assert(ovl_acquire(slot, &token, &view) == -1 && errno == EBUSY);
    assert(!memcmp(&view, &before, sizeof(view)) && !token._slot);
    assert(ovl_unload(slot) == -1 && errno == EBUSY);
    assert(ovl_slot_destroy(slot) == -1 && errno == EBUSY);
    ovl_load_result_t result;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) == -1 && errno == EBUSY);
    assert(!result.image_admitted && result.error == EBUSY);
}

/* Fault-injection boundary model, NOT the production VM/cache implementation.
   Kernel's actual-code suite and the combined target fixture are separate gates. */
#include "vm_boundary_test.inc"

static void observer(ovl_slot_t *slot, bool loaded, void *context) {
    assert(!lock_depth && context == &trusted);
    if(loaded) {
        assert(!live_fd && cache_calls == 1);
        ++loads;
    }
    else {
        assert(cache_calls == 0);
        ++unloads;
    }
    busy_checks(slot);
}

static void expect_active(ovl_slot_t *slot, uint64_t generation) {
    ovl_token_t token = {0};
    ovl_view_t view;
    assert(!ovl_acquire(slot, &token, &view));
    assert(view.base == slot->options.base && view.bytes == 8192);
    assert(view.generation == generation);
    assert(!memcmp(&view.identity, &trusted, sizeof(trusted)));
    assert(!ovl_release(slot, &token));
}

static void expect_empty(ovl_slot_t *slot) {
    ovl_token_t token = {0};
    ovl_view_t view;
    memset(&view, 0x79, sizeof(view));
    ovl_view_t before = view;
    assert(ovl_acquire(slot, &token, &view) == -1 && errno == ENOENT);
    assert(!token._slot && !memcmp(&view, &before, sizeof(view)));
}

static void load_ok(ovl_slot_t *slot, uint64_t generation) {
    ovl_load_result_t result;
    assert(!ovl_load(slot, "/cd/member.dco", &trusted, &result));
    assert(result.image_admitted && !result.error && !result.source_close_error);
    assert(!live_fd && closes == 1 && loads == 1 && cache_calls == 1);
    assert(cursor == image_bytes);
    expect_active(slot, generation);
}

static uint32_t read32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}

static void package_tests(void) {
    ovl_package_t plan;
    assert(!ovl_package_header(image, 0x8c100000, 8192, image_bytes, &plan));
    assert(!ovl_package_segments(image + 64, &plan));
    /* Every constrained header field and every reserved byte. */
    const size_t fields[] = {0, 4, 6, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44};
    for(size_t i = 0; i < sizeof(fields) / sizeof(*fields); ++i) {
        size_t at = fields[i];
        image[at] ^= 1;
        assert(ovl_package_header(image, 0x8c100000, 8192, image_bytes, &plan) < 0);
        image[at] ^= 1;
    }
    for(size_t at = 48; at < 64; ++at) {
        image[at] = 1;
        assert(ovl_package_header(image, 0x8c100000, 8192, image_bytes, &plan) < 0);
        image[at] = 0;
    }
    assert(!ovl_package_header(image, 0x8c100000, 8192, image_bytes, &plan));
    const struct { size_t at; uint32_t value; } changes[] = {
        {64, UINT32_MAX}, {68, UINT32_MAX}, {72, 0}, {72, UINT32_MAX},
        {76, UINT32_MAX}, {80, 0}, {80, 7}, {80, 8}, {84, 3},
        {84, 0x80000000}, {96, 0}, {96, 4}, {100, 9}, {104, 0xffffffff},
        {108, 0xffffffff}
    };
    for(size_t i = 0; i < sizeof(changes) / sizeof(*changes); ++i) {
        uint32_t old = read32(image + changes[i].at);
        write32(image + changes[i].at, changes[i].value);
        assert(ovl_package_segments(image + 64, &plan) < 0);
        write32(image + changes[i].at, old);
    }
    for(size_t at = 88; at < 96; ++at) {
        image[at] = 1;
        assert(ovl_package_segments(image + 64, &plan) < 0);
        image[at] = 0;
    }
    assert(!ovl_package_segments(image + 64, &plan));
    uint8_t slot[8192];
    for(size_t chunk = 1; chunk <= 257; chunk += 64) {
        memset(slot, 0xa5, sizeof(slot));
        for(uint32_t offset = 0; offset < plan.elf_bytes;) {
            size_t n = plan.elf_bytes - offset;
            if(n > chunk) n = chunk;
            ovl_package_scatter(&plan, slot, offset, image + plan.elf_offset + offset, n);
            offset += n;
        }
        ovl_package_zero(&plan, slot);
        assert(!memcmp(slot, image + plan.elf_offset + 0x100, 4));
        assert(!memcmp(slot + 4096, image + plan.elf_offset + 0x104, 4));
        for(size_t i = 4; i < 4096; ++i) assert(slot[i] == 0xa5);
        for(size_t i = 4100; i < 4104; ++i) assert(!slot[i]);
        assert(slot[4104] == 0xa5 && slot[8191] == 0xa5);
    }
    /* Reordered/shared source extents remain legal. Output overlap never is. */
    write32(image + 108, 0x100);
    assert(!ovl_package_segments(image + 64, &plan));
    write32(image + 108, 0x104);
    /* Mutation fuzzing stays within the retained maximum-sized prefix. Only
       call descriptor admission after its bounded header has been accepted. */
    uint8_t fuzz[OVL_PREFIX_BYTES];
    uint32_t random = 0x44436f40;
    for(unsigned trial = 0; trial < 10000; ++trial) {
        memset(fuzz, 0, sizeof(fuzz));
        memcpy(fuzz, image, 128);
        for(unsigned change = 0; change < 3; ++change) {
            random = random * 1664525u + 1013904223u;
            size_t at = random % sizeof(fuzz);
            random = random * 1664525u + 1013904223u;
            fuzz[at] ^= (uint8_t)(random >> 24);
        }
        if(!ovl_package_header(fuzz, 0x8c100000, 8192, image_bytes, &plan))
            (void)ovl_package_segments(fuzz + 64, &plan);
    }
    puts("PASS: bounded package parsing, exact BSS/gaps and chunk scatter");
}

static void profile_tests(void) {
    ovl_package_t plan;
    const uint32_t base = 0x101ff000;
    assert(!ovl_package_header_profile(image, base, 8192, image_bytes, OVL_PROFILE_VM, &plan));
    assert(plan.profile == OVL_PROFILE_VM);
    assert(!ovl_package_segments(image + 64, &plan));
    /* Legacy/P1 callers cannot accidentally opt in to translated execution. */
    assert(ovl_package_header(image, base, 8192, image_bytes, &plan) < 0);
    assert(ovl_package_header_profile(image, base, 8192, image_bytes, 3, &plan) < 0);
    assert(ovl_package_header_profile(image, base, 8192, image_bytes, 0, &plan) < 0);
    write32(image + 28, OVL_PROFILE_P1);
    assert(ovl_package_header_profile(image, base, 8192, image_bytes, OVL_PROFILE_VM, &plan) < 0);
    write32(image + 28, OVL_PROFILE_VM);
    const uint32_t bad_bases[] = {0, 0x1001, 0x7bfff000, 0x7c000000, 0x8c100000, 0xfffff000};
    for(size_t i = 0; i < sizeof(bad_bases) / sizeof(*bad_bases); ++i) {
        write32(image + 16, bad_bases[i]);
        assert(ovl_package_header_profile(image, bad_bases[i], 8192, image_bytes,
                                          OVL_PROFILE_VM, &plan) < 0);
    }
    const uint32_t good_bases[] = {0x1000, base, 0x1ffff000, 0x7bffe000};
    for(size_t i = 0; i < sizeof(good_bases) / sizeof(*good_bases); ++i) {
        write32(image + 16, good_bases[i]);
        assert(!ovl_package_header_profile(image, good_bases[i], 8192, image_bytes,
                                           OVL_PROFILE_VM, &plan));
        assert(!ovl_package_segments(image + 64, &plan));
    }
    write32(image + 16, base);
    const uint32_t bad_sizes[] = {0, 8193, 0x101000, UINT32_MAX};
    for(size_t i = 0; i < sizeof(bad_sizes) / sizeof(*bad_sizes); ++i) {
        write32(image + 20, bad_sizes[i]);
        assert(ovl_package_header_profile(image, base, bad_sizes[i], image_bytes,
                                          OVL_PROFILE_VM, &plan) < 0);
    }
    write32(image + 20, 8192);
    assert(!ovl_package_header_profile(image, base, 8192, image_bytes, OVL_PROFILE_VM, &plan));
    /* Distinct byte ranges sharing one 4 KiB page cannot disagree on permissions. */
    write32(image + 96, 64);
    assert(ovl_package_segments(image + 64, &plan) < 0);
    write32(image + 112, 5);
    assert(!ovl_package_segments(image + 64, &plan));
    write32(image + 112, 6);
    write32(image + 96, 4096);
    assert(!ovl_package_segments(image + 64, &plan));
    puts("PASS: explicit translated profile, bounds and 4 KiB permission admission (no execution)");
}

static ovl_slot_t *interleave_slot;
static void *other_thread(void *unused) {
    (void)unused;
    busy_checks(interleave_slot);
    return NULL;
}
static void interleave(void) {
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, other_thread, NULL));
    assert(!pthread_join(thread, NULL));
    read_observer = NULL;
}

static void runtime_tests(void) {
    uint8_t *ram = mmap((void *)0x8c100000, 8192, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(ram == (void *)0x8c100000); /* Hint only: never replace an existing map. */
    ovl_slot_options_t options = {ram, 8192, observer, &trusted};
    ovl_slot_t *slot = NULL;
    reset_io();
    for(unsigned i = 0; i < 3; ++i) {
        test_sr = i == 0 ? 0x10 : i == 1 ? 0x10000000 : 0;
        in_irq = i == 2;
        assert(ovl_slot_create(&options, &slot) < 0 && errno == EPERM && !slot);
        assert(!allocations);
    }
    test_sr = in_irq = 0;
    for(unsigned i = 0; i < 5; ++i) {
        ovl_slot_options_t bad = options;
        if(i == 0) bad.base = (void *)0xac100000;
        if(i == 1) bad.bytes = 0;
        if(i == 2) bad.bytes = 8193;
        if(i == 3) bad.base = (void *)0x8cfff000;
        if(i == 4) bad.bytes = OVL_MAX_SLOT_BYTES + 32;
        assert(ovl_slot_create(&bad, &slot) < 0 && errno == EINVAL && !slot);
    }
    assert(!ovl_slot_create(&options, &slot));
    expect_empty(slot);
    assert(!ovl_unload(slot) && !unloads);
    memset(ram, 0xa5, 8192);
    read_cap = 1;
    load_ok(slot, 1);
    assert(reads == (int)image_bytes && ram[4] == 0xa5 && !ram[4100]);

    /* Address-registered residency rejects copies, wrong slots and double use. */
    ovl_token_t a = {0}, b = {0}, copied;
    ovl_view_t view;
    assert(!ovl_acquire(slot, &a, &view));
    assert(!ovl_acquire(slot, &b, &view));
    copied = a;
    assert(ovl_release(slot, &copied) < 0 && errno == EINVAL);
    assert(ovl_acquire(slot, &a, &view) < 0 && errno == EBUSY);
    assert(ovl_unload(slot) < 0 && errno == EBUSY);
    ovl_load_result_t result;
    int old_reads = reads;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EBUSY);
    assert(reads == old_reads);
    ovl_slot_t *other = NULL;
    ovl_slot_options_t second = options;
    second.base = (void *)0x8c200000;
    assert(!ovl_slot_create(&second, &other));
    assert(ovl_release(other, &a) < 0 && errno == EINVAL);
    assert(!ovl_slot_destroy(other));
    assert(!ovl_release(slot, &a) && !ovl_release(slot, &b));
    assert(ovl_release(slot, &a) < 0 && errno == EINVAL);

    /* Every preparation failure preserves bytes, identity and old generation. */
    uint8_t before[8192];
    memcpy(before, ram, sizeof(before));
    for(unsigned fault = 0; fault < 14; ++fault) {
        reset_io();
        if(fault == 0) allocation_error = 1;
        if(fault == 1) open_error = EACCES;
        if(fault == 2) stat_error = EIO;
        if(fault == 3) nonregular = true;
        if(fault == 4) size_delta = 1;
        if(fault == 5) size_delta = -1;
        if(fault == 6) eof_at = 63;
        if(fault == 7) fail_at = 80;
        if(fault == 8) image[0] ^= 1;
        if(fault == 9) image[88] ^= 1;
        if(fault == 10) oversized = true;
        if(fault == 11) backend = 0;
        if(fault == 12) { fail_at = 0; close_error = ENOSPC; }
        if(fault == 13) write32(image + 28, OVL_PROFILE_VM);
        assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0);
        assert(result.error && !result.image_admitted && !live_fd);
        assert(!cache_calls && !loads && !unloads);
        assert(result.source_close_error == (fault == 12 ? ENOSPC : 0));
        if(fault == 12) assert(result.error == EIO);
        if(fault == 8) image[0] ^= 1;
        if(fault == 9) image[88] ^= 1;
        if(fault == 13) write32(image + 28, OVL_PROFILE_P1);
        assert(!memcmp(before, ram, sizeof(before)));
        expect_active(slot, 1);
    }
    reset_io();
    slot->generation = UINT64_MAX;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EOVERFLOW);
    assert(!reads && !opens && !unloads);
    slot->generation = 1;
    for(unsigned i = 0; i < 4; ++i) {
        const char *paths[] = {"/rd/member.dco", "/cd/../member.dco", "/cd/./member.dco", "/cd//member.dco"};
        assert(ovl_load(slot, paths[i], &trusted, &result) < 0 && errno == EINVAL);
    }
    /* Read failure, EOF, whole-file digest and close failure after retirement. */
    uint64_t generation = 1;
    for(unsigned fault = 0; fault < 5; ++fault) {
        reset_io();
        if(fault == 0) fail_at = 128;
        if(fault == 1) eof_at = image_bytes - 1;
        if(fault == 2) image[image_bytes - 1] ^= 1; /* Non-loadable metadata. */
        if(fault == 3) close_error = EIO;
        if(fault == 4) { fail_at = 128; close_error = ENOSPC; }
        assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0);
        assert(!result.image_admitted && !live_fd && closes == 1);
        assert(unloads == 1 && !loads && !cache_calls);
        if(fault == 4) assert(result.error == EIO && result.source_close_error == ENOSPC);
        expect_empty(slot);
        if(fault == 2) image[image_bytes - 1] ^= 1;
        reset_io();
        load_ok(slot, ++generation);
    }
    reset_io();
    interleave_slot = slot;
    read_observer = interleave;
    load_ok(slot, ++generation);
    assert(unloads == 1);
    reset_io();
    in_irq = 1;
    assert(ovl_acquire(slot, &a, &view) < 0 && errno == EPERM);
    assert(ovl_release(slot, &a) < 0 && errno == EPERM);
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EPERM);
    assert(ovl_unload(slot) < 0 && errno == EPERM);
    assert(ovl_slot_destroy(slot) < 0 && errno == EPERM);
    assert(!opens && !allocations);
    in_irq = 0;
    assert(ovl_slot_destroy(slot) < 0 && errno == EBUSY);
    assert(!ovl_unload(slot) && unloads == 1);
    assert(!ovl_unload(slot) && unloads == 1);
    assert(!ovl_slot_destroy(slot));
    assert(!munmap(ram, 8192));
    puts("PASS: actual runtime preparation/retirement, tokens, close, reentry and thread interleaving");
}

static void large_tests(void) {
    uint8_t *ram = mmap((void *)0x8c100000, 8192, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(ram == (void *)0x8c100000);
    ovl_slot_options_t options = {ram, 8192, observer, &trusted};
    ovl_slot_t *slot;
    reset_io();
    assert(!ovl_slot_create(&options, &slot));
    memset(ram, 0xa5, 8192);
    read_cap = 2047;
    load_ok(slot, 1);
    assert(!memcmp(ram, "TEXTedge", 8) && !memcmp(ram + 4096, "DATA", 4));
    for(size_t i = 8; i < 4096; ++i) assert(ram[i] == 0xa5);
    for(size_t i = 4100; i < 4104; ++i) assert(!ram[i]);
    assert(ram[4104] == 0xa5);
    reset_io();
    fail_at = 128 + 32768 + 1;
    ovl_load_result_t result;
    assert(ovl_load(slot, "/cd/member.dco", &trusted, &result) < 0 && errno == EIO);
    assert(!result.image_admitted && closes == 1 && !cache_calls && unloads == 1);
    expect_empty(slot);
    assert(!ovl_slot_destroy(slot));
    assert(!munmap(ram, 8192));
    puts("PASS: multi-block single-pass load, unordered input extents and partial second-block failure");
}

static uint8_t *read_file(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb");
    assert(f && !fseek(f, 0, SEEK_END));
    long size = ftell(f);
    assert(size >= 0 && !fseek(f, 0, SEEK_SET));
    uint8_t *data = malloc((size_t)size + 1);
    assert(data && fread(data, 1, size, f) == (size_t)size);
    assert(!fclose(f));
    *bytes = (size_t)size;
    return data;
}

int main(int argc, char **argv) {
    assert(argc == 3 || argc == 4);
    if(!strcmp(argv[1], "--profile")) {
        image = read_file(argv[2], &image_bytes);
        assert(image_bytes >= 128);
        profile_tests();
        free(image);
        return 0;
    }
    if(!strcmp(argv[1], "--sha")) {
        size_t bytes;
        uint8_t *data = read_file(argv[2], &bytes);
        const size_t chunks[] = {1, 7, 55, 64, 65, 32768};
        for(size_t i = 0; i < sizeof(chunks) / sizeof(*chunks); ++i) {
            ovl_sha256_t hash;
            uint8_t digest[32];
            ovl_sha256_init(&hash);
            for(size_t offset = 0; offset < bytes;) {
                size_t n = bytes - offset;
                if(n > chunks[i]) n = chunks[i];
                ovl_sha256_update(&hash, data + offset, n);
                offset += n;
            }
            ovl_sha256_final(&hash, digest);
            for(size_t j = 0; j < sizeof(digest); ++j) printf("%02x", digest[j]);
            puts("");
        }
        free(data);
        return 0;
    }
    bool large = !strcmp(argv[1], "--large");
    bool vm = !strcmp(argv[1], "--vm");
    assert(argc == (large || vm ? 4 : 3));
    image = read_file(argv[large || vm ? 2 : 1], &image_bytes);
    trusted.package_bytes = image_bytes;
    const char *digest = argv[large || vm ? 3 : 2];
    assert(strlen(digest) == 64);
    for(unsigned i = 0; i < 32; ++i) {
        unsigned value;
        assert(sscanf(digest + 2 * i, "%2x", &value) == 1);
        trusted.sha256[i] = value;
    }
    if(vm) vm_runtime_tests();
    else if(large) large_tests();
    else {
        package_tests();
        runtime_tests();
    }
    free(image);
    return 0;
}
