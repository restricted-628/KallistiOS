/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   Independent implementation of the SHA-256 mathematics in RFC 6234,
   sections 4.1, 5.1 and 6.1-6.2 (not its sample implementation):
   https://www.rfc-editor.org/rfc/rfc6234#section-6.2
*/
#include "sha256.h"
#include <string.h>

static const uint32_t round_constant[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotate(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32 - count));
}

static uint32_t big_endian(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void compress(ovl_sha256_t *hash, const uint8_t *block) {
    uint32_t words[16];
    uint32_t a = hash->state[0], b = hash->state[1];
    uint32_t c = hash->state[2], d = hash->state[3];
    uint32_t e = hash->state[4], f = hash->state[5];
    uint32_t g = hash->state[6], h = hash->state[7];

    for(unsigned i = 0; i < 64; ++i) {
        unsigned index = i & 15;
        if(i < 16)
            words[index] = big_endian(block + 4 * i);
        else {
            uint32_t x = words[(i - 15) & 15];
            uint32_t y = words[(i - 2) & 15];
            words[index] += (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) +
                           words[(i - 7) & 15] +
                           (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
        }
        uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                      ((e & f) ^ (~e & g)) + round_constant[i] + words[index];
        uint32_t t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    hash->state[0] += a; hash->state[1] += b;
    hash->state[2] += c; hash->state[3] += d;
    hash->state[4] += e; hash->state[5] += f;
    hash->state[6] += g; hash->state[7] += h;
}

void ovl_sha256_init(ovl_sha256_t *hash) {
    *hash = (ovl_sha256_t){ .state = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    }};
}

void ovl_sha256_update(ovl_sha256_t *hash, const void *data, size_t bytes) {
    const uint8_t *input = data;
    hash->bytes += bytes;
    while(bytes) {
        size_t count = sizeof(hash->block) - hash->used;
        if(count > bytes) count = bytes;
        memcpy(hash->block + hash->used, input, count);
        hash->used += count;
        input += count;
        bytes -= count;
        if(hash->used == sizeof(hash->block)) {
            compress(hash, hash->block);
            hash->used = 0;
        }
    }
}

void ovl_sha256_final(ovl_sha256_t *hash, uint8_t digest[32]) {
    uint64_t bits = hash->bytes * 8;
    hash->block[hash->used++] = 0x80;
    if(hash->used > 56) {
        memset(hash->block + hash->used, 0, 64 - hash->used);
        compress(hash, hash->block);
        hash->used = 0;
    }
    memset(hash->block + hash->used, 0, 56 - hash->used);
    for(unsigned i = 0; i < 8; ++i)
        hash->block[63 - i] = (uint8_t)(bits >> (8 * i));
    compress(hash, hash->block);
    for(unsigned i = 0; i < 32; ++i)
        digest[i] = (uint8_t)(hash->state[i / 4] >> (24 - 8 * (i & 3)));
}
