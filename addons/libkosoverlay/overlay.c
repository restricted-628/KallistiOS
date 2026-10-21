/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos/overlay.h>
#include <kos/cache.h>
#include <kos/fs.h>
#include <kos/irq.h>
#include <kos/mutex.h>
#include <arch/arch.h>
#include <arch/stack.h>
#include <dc/fs_iso9660.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "package.h"
#include "sha256.h"

struct ovl_slot {
    mutex_t mutex;
    ovl_slot_options_t options;
    ovl_token_t *tokens;
    ovl_expected_t identity;
    uint64_t generation;
    uint32_t entry_offset;
    bool active, operation;
};

static int error(int code) {
    errno = code;
    return -1;
}

static int eligible(void) {
    uint32_t sr;
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    return irq_inside_int() || (sr & 0x100000f0u) ? error(EPERM) : 0;
}

/* Direct P1/P2 aliases only. Excluding translated aliases, overlapping slots
   and unreserved heap/stack storage remains an explicit caller obligation. */
static bool aliases(const ovl_slot_options_t *o, const void *pointer, size_t n) {
    uintptr_t address = (uintptr_t)pointer;
    if(!n) return false;
    if(n - 1 > UINTPTR_MAX - address) return true;
    if(address < 0x80000000u || address >= 0xc0000000u) return false;
    uintptr_t physical = address & 0x1fffffffu;
    uintptr_t base = (uintptr_t)o->base & 0x1fffffffu;
    return physical < base + o->bytes && n > (physical < base ? base - physical : 0);
}

/* Once operation is owned, this normal untimed lock cannot fail under the
   live-owner/no-foreign-error-wake contract (the same restriction as VM4).
   In particular, do not release operation before callbacks/cleanup retire. */
static void finish_lock(ovl_slot_t *slot) {
    int status = mutex_lock(&slot->mutex);
    assert(status == 0);
    (void)status;
}

int ovl_slot_create(const ovl_slot_options_t *options, ovl_slot_t **out) {
    if(eligible() < 0) return -1;
    if(!options || !out) return error(EINVAL);
    uintptr_t base = (uintptr_t)options->base;
    uintptr_t top = _arch_mem_top;
    if(base < 0x8c000000u || base >= top || (base & 31) ||
       !options->bytes || (options->bytes & 31) ||
       options->bytes > OVL_MAX_SLOT_BYTES ||
       top - base < THD_KERNEL_STACK_SIZE ||
       options->bytes > top - base - THD_KERNEL_STACK_SIZE ||
       aliases(options, options, sizeof(*options)) ||
       aliases(options, out, sizeof(*out)) ||
       aliases(options, (const void *)options->observer, 1) ||
       aliases(options, options->context, 1))
        return error(EINVAL);
    ovl_slot_t *slot = calloc(1, sizeof(*slot));
    if(!slot) return error(ENOMEM);
    if(aliases(options, slot, sizeof(*slot))) {
        free(slot);
        return error(EINVAL);
    }
    slot->options = *options;
    if(mutex_init(&slot->mutex, MUTEX_TYPE_NORMAL) < 0) {
        int saved = errno;
        free(slot);
        return error(saved);
    }
    *out = slot;
    return 0;
}

int ovl_acquire(ovl_slot_t *slot, ovl_token_t *token, ovl_view_t *view) {
    if(eligible() < 0) return -1;
    if(!slot || !token || !view ||
       aliases(&slot->options, token, sizeof(*token)) ||
       aliases(&slot->options, view, sizeof(*view)))
        return error(EINVAL);
    if(mutex_lock(&slot->mutex) < 0) return -1;
    int code = 0;
    if(slot->operation || token->_slot || token->_next || token->_generation)
        code = EBUSY;
    else if(!slot->active)
        code = ENOENT;
    else {
        *token = (ovl_token_t){ slot, slot->tokens, slot->generation };
        slot->tokens = token;
        *view = (ovl_view_t){
            .base = slot->options.base, .bytes = slot->options.bytes,
            .entry = (ovl_entry_t)((uintptr_t)slot->options.base + slot->entry_offset),
            .generation = slot->generation, .identity = slot->identity
        };
    }
    mutex_unlock(&slot->mutex);
    return code ? error(code) : 0;
}

int ovl_release(ovl_slot_t *slot, ovl_token_t *token) {
    if(eligible() < 0) return -1;
    if(!slot || !token || aliases(&slot->options, token, sizeof(*token)))
        return error(EINVAL);
    if(mutex_lock(&slot->mutex) < 0) return -1;
    int code = 0;
    ovl_token_t **link = &slot->tokens;
    while(*link && *link != token) link = &(*link)->_next;
    if(slot->operation)
        code = EBUSY;
    else if(!*link || token->_slot != slot || token->_generation != slot->generation)
        code = EINVAL;
    else {
        *link = token->_next;
        *token = (ovl_token_t){0};
    }
    mutex_unlock(&slot->mutex);
    return code ? error(code) : 0;
}

static int claim(ovl_slot_t *slot, bool load) {
    if(mutex_lock(&slot->mutex) < 0) return -1;
    int code = 0;
    if(slot->operation || slot->tokens) code = EBUSY;
    else if(load && slot->generation == UINT64_MAX) code = EOVERFLOW;
    else slot->operation = true;
    mutex_unlock(&slot->mutex);
    return code ? error(code) : 0;
}

static void notify(ovl_slot_t *slot, bool loaded) {
    if(slot->options.observer)
        slot->options.observer(slot, loaded, slot->options.context);
}

int ovl_unload(ovl_slot_t *slot) {
    if(eligible() < 0) return -1;
    if(!slot) return error(EINVAL);
    if(claim(slot, false) < 0) return -1;
    if(slot->active) notify(slot, false);
    finish_lock(slot);
    slot->active = false;
    slot->operation = false;
    mutex_unlock(&slot->mutex);
    return 0;
}

int ovl_slot_destroy(ovl_slot_t *slot) {
    if(eligible() < 0) return -1;
    if(!slot) return error(EINVAL);
    if(mutex_lock(&slot->mutex) < 0) return -1;
    bool busy = slot->operation || slot->active || slot->tokens;
    mutex_unlock(&slot->mutex);
    if(busy) return error(EBUSY);
    if(mutex_destroy(&slot->mutex) < 0) return -1;
    free(slot);
    return 0;
}

static int read_exact(file_t fd, uint8_t *buffer, size_t bytes) {
    while(bytes) {
        ssize_t count = fs_read(fd, buffer, bytes);
        if(count < 0) return -1; /* Even a partially modified buffer is unusable. */
        if(!count || (size_t)count > bytes) return error(EIO);
        buffer += count;
        bytes -= (size_t)count;
    }
    return 0;
}

static bool source_path(const char *path) {
    if(strncmp(path, "/cd/", 4)) return false;
    const char *part = path + 4;
    for(;;) {
        const char *end = strchr(part, '/');
        size_t n = end ? (size_t)(end - part) : strlen(part);
        if(!n || (n == 1 && part[0] == '.') ||
           (n == 2 && part[0] == '.' && part[1] == '.')) return false;
        if(!end) return true;
        part = end + 1;
    }
}

int ovl_load(ovl_slot_t *slot, const char *path, const ovl_expected_t *expected,
             ovl_load_result_t *result) {
    if(eligible() < 0) return -1;
    if(!slot || !path || !expected || !result ||
       aliases(&slot->options, path, 1) ||
       aliases(&slot->options, expected, sizeof(*expected)) ||
       aliases(&slot->options, result, sizeof(*result)))
        return error(EINVAL);
    /* Inputs remain immutable/resident through return; make identity independent
       of output before any mutation or notification. */
    ovl_expected_t wanted = *expected;
    *result = (ovl_load_result_t){0};
    size_t path_bytes = strnlen(path, PATH_MAX);
    int code = 0;
    if(path_bytes == PATH_MAX || !source_path(path) ||
       aliases(&slot->options, path, path_bytes + 1) ||
       wanted.package_bytes > INT32_MAX || wanted.package_bytes < OVL_HEADER_BYTES)
        code = EINVAL;
    else if(claim(slot, true) < 0)
        code = errno;
    if(code) {
        result->error = code;
        return error(code);
    }

    file_t fd = FILEHND_INVALID;
    uint8_t *staging = NULL;
    uint8_t prefix[OVL_PREFIX_BYTES], digest[32];
    ovl_package_t package;
    ovl_sha256_t hash;
    struct stat status;
    bool destructive = false;
    if(fs_iso9660_get_backend() != FS_ISO9660_BACKEND_DIRECT) {
        code = ENOTSUP;
        goto cleanup;
    }
    staging = aligned_alloc(32, OVL_STAGING_BYTES);
    if(!staging) { code = ENOMEM; goto cleanup; }
    if(aliases(&slot->options, staging, OVL_STAGING_BYTES) ||
       aliases(&slot->options, prefix, sizeof(prefix)) ||
       aliases(&slot->options, &package, sizeof(package))) {
        code = EINVAL;
        goto cleanup;
    }
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) { code = errno; goto cleanup; }
    if(fs_fstat(fd, &status) < 0) { code = errno; goto cleanup; }
    if(!S_ISREG(status.st_mode) || status.st_size < 0 ||
       (uint64_t)status.st_size != wanted.package_bytes) {
        code = ENOEXEC;
        goto cleanup;
    }
    if(read_exact(fd, prefix, OVL_HEADER_BYTES) < 0 ||
       ovl_package_header(prefix, (uint32_t)(uintptr_t)slot->options.base,
                          slot->options.bytes, wanted.package_bytes, &package) < 0 ||
       read_exact(fd, prefix + OVL_HEADER_BYTES,
                  package.count * OVL_DESCRIPTOR_BYTES) < 0 ||
       ovl_package_segments(prefix + OVL_HEADER_BYTES, &package) < 0) {
        code = errno;
        goto cleanup;
    }

    /* No future failure may re-admit the retired generation, even if no bytes
       have been overwritten yet. Notifications still see operation owned. */
    destructive = true;
    if(slot->active) notify(slot, false);
    ovl_sha256_init(&hash);
    ovl_sha256_update(&hash, prefix, package.elf_offset);
    for(uint32_t offset = 0; offset < package.elf_bytes;) {
        size_t bytes = package.elf_bytes - offset;
        if(bytes > OVL_STAGING_BYTES) bytes = OVL_STAGING_BYTES;
        if(read_exact(fd, staging, bytes) < 0) { code = errno; goto cleanup; }
        ovl_sha256_update(&hash, staging, bytes);
        ovl_package_scatter(&package, slot->options.base, offset, staging, bytes);
        offset += bytes;
    }
    ovl_sha256_final(&hash, digest);
    if(memcmp(digest, wanted.sha256, sizeof(digest))) {
        code = ENOEXEC;
        goto cleanup;
    }
    ovl_package_zero(&package, slot->options.base);

cleanup:
    if(fd != FILEHND_INVALID && fs_close(fd) < 0) {
        result->source_close_error = errno;
        if(!code) code = result->source_close_error;
    }
    free(staging);
    if(!code) {
        icache_sync_range((uintptr_t)slot->options.base, slot->options.bytes);
        notify(slot, true);
    }
    finish_lock(slot);
    if(!code) {
        slot->identity = wanted;
        slot->entry_offset = package.entry_offset;
        ++slot->generation;
        slot->active = true;
        result->image_admitted = true;
    }
    else if(destructive)
        slot->active = false;
    slot->operation = false;
    mutex_unlock(&slot->mutex);
    result->error = code;
    return code ? error(code) : 0;
}
