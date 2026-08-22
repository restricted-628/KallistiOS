/* KallistiOS ##version##

   sq-safety.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <arch/mmu.h>
#include <dc/sq.h>

#include <errno.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQ_RECURSION_DEPTH 8u

static alignas(32) uint8_t source[64];
static alignas(32) uint8_t destination[64];

static int fail(const char *operation) {
    printf("Store-queue probe failed: %s (errno=%d: %s)\n",
           operation, errno, strerror(errno));
    return EXIT_FAILURE;
}

static int verify_copy(void) {
    memset(destination, 0, sizeof(destination));
    dcache_purge_range((uintptr_t)destination, sizeof(destination));

    if(!sq_cpy(destination, source, sizeof(source)))
        return -1;

    dcache_inval_range((uintptr_t)destination, sizeof(destination));
    if(memcmp(destination, source, sizeof(source))) {
        errno = EIO;
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    bool initialized_mmu = false;
    bool recursion_failed = false;
    unsigned int depth = 0;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    for(size_t i = 0; i < sizeof(source); i++)
        source[i] = (uint8_t)(i ^ 0xa5u);

    errno = 0;
    if(sq_lock(destination + 1) || errno != EINVAL) {
        result = fail("unaligned lock rejection");
        goto out;
    }

    errno = 0;
    if(sq_cpy(destination, source + 1, sizeof(source)) || errno != EINVAL) {
        result = fail("unaligned copy source rejection");
        goto out;
    }

    errno = 0;
    if(sq_set32(destination, 0x12345678u, sizeof(destination) - 1) ||
       errno != EINVAL) {
        result = fail("partial burst rejection");
        goto out;
    }

    errno = 0;
    if(sq_cpy((void *)0xffffffe0u, source, sizeof(source)) ||
       errno != EINVAL) {
        result = fail("wrapping destination rejection");
        goto out;
    }

    if(mmu_enabled()) {
        errno = EBUSY;
        result = fail("probe requires MMU-off startup");
        goto out;
    }

    for(depth = 0; depth < SQ_RECURSION_DEPTH; depth++) {
        if(!sq_lock(destination)) {
            result = fail("recursive lock acquisition");
            recursion_failed = true;
            goto unlock;
        }
    }

    errno = 0;
    if(sq_lock(destination)) {
        depth++;
        result = fail("recursive capacity rejection");
        recursion_failed = true;
        goto unlock;
    }
    if(errno != EOVERFLOW) {
        result = fail("recursive overflow errno");
        recursion_failed = true;
        goto unlock;
    }

unlock:
    while(depth) {
        sq_unlock();
        depth--;
    }

    if(recursion_failed)
        goto out;

    if(verify_copy() < 0) {
        result = fail("MMU-off copy");
        goto out;
    }

    if(!mmu_enabled()) {
        mmu_init_basic();
        initialized_mmu = true;
    }

    if(!mmu_enabled() || verify_copy() < 0) {
        result = fail("MMU-on copy");
        goto out;
    }

    printf("KOSSQ recursion=8 validation=1 mmu=1\n");
    result = EXIT_SUCCESS;

out:
    if(initialized_mmu)
        mmu_shutdown_basic();
    return result;
}
