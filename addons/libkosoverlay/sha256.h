/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_OVERLAY_SHA256_H
#define KOS_OVERLAY_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct ovl_sha256 {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} ovl_sha256_t;

/* Private package identity primitive, not a public cryptographic API.
   Total input is bounded by the DCo@ v1 INT32_MAX package limit. */
void ovl_sha256_init(ovl_sha256_t *hash);
void ovl_sha256_update(ovl_sha256_t *hash, const void *data, size_t bytes);
void ovl_sha256_final(ovl_sha256_t *hash, uint8_t digest[32]);

#endif
