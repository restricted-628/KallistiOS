/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   KOS-side regression probes for the unmodified SH4ZAM 0.8.1 dependency.
*/
#ifndef SH4ZAM_RELEASE_FIXTURES_H
#define SH4ZAM_RELEASE_FIXTURES_H

static bool verify_release_memory(void) {
    uint8_t source[256] __attribute__((aligned(32)));
    uint8_t destination[256] __attribute__((aligned(32)));

    for(size_t i = 0; i < sizeof(source); ++i)
        source[i] = (uint8_t)(i * 37u + 11u);
    for(unsigned kind = 0; kind < 3; ++kind) {
        size_t alignment = kind ? (size_t)1u << kind : 1u;
        for(size_t src = 0; src < 8; src += alignment) {
            for(size_t dst = 0; dst < 8; dst += alignment) {
                for(size_t bytes = 0; bytes <= 132; bytes += alignment) {
                    void *returned;
                    memset(destination, 0xa5, sizeof(destination));
                    if(kind == 1)
                        returned = shz_memcpy2(destination + dst, source + src, bytes);
                    else if(kind == 2)
                        returned = shz_memcpy4(destination + dst, source + src, bytes);
                    else
                        returned = shz_memcpy(destination + dst, source + src, bytes);
                    if(returned != destination + dst) return false;
                    for(size_t i = 0; i < sizeof(destination); ++i) {
                        uint8_t expected = i >= dst && i - dst < bytes ?
                            source[src + i - dst] : 0xa5;
                        if(destination[i] != expected) {
                            printf("SH4ZAM copy mismatch: kind=%u src=%u dst=%u bytes=%u at=%u\n",
                                   kind, (unsigned)src, (unsigned)dst,
                                   (unsigned)bytes, (unsigned)i);
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

static bool verify_release_math(void) {
    static const float scales[][3] = {
        { 0, 0, 0 }, { 0, 2, 3 }, { 2, 0, 3 }, { 2, 3, 0 }, { -2, 3, -4 }
    };
    static const uint16_t angles[] = { 0, 16384, 32768, 49152 };
    static const float expected_sin[] = { 0, 1, 0, -1 };
    static const float expected_cos[] = { 1, 0, -1, 0 };
    shz_mat4x4_t saved, matrix, observed;
    bool passed = true;

    shz_xmtrx_store_4x4(&saved);
    for(size_t i = 0; i < sizeof(scales) / sizeof(*scales); ++i) {
        shz_mat4x4_init_scale(&matrix, scales[i][0], scales[i][1], scales[i][2]);
        shz_vec3_t scale = shz_mat4x4_get_scale(&matrix);
        shz_xmtrx_load_4x4(&matrix);
        shz_vec3_t resident_scale = shz_xmtrx_get_scale();
        passed &= isfinite(scale.x) && isfinite(scale.y) && isfinite(scale.z) &&
                  isfinite(resident_scale.x) && isfinite(resident_scale.y) &&
                  isfinite(resident_scale.z) &&
                  close_enough(scale.x, fabsf(scales[i][0])) &&
                  close_enough(scale.y, fabsf(scales[i][1])) &&
                  close_enough(scale.z, fabsf(scales[i][2])) &&
                  close_enough(resident_scale.x, fabsf(scales[i][0])) &&
                  close_enough(resident_scale.y, fabsf(scales[i][1])) &&
                  close_enough(resident_scale.z, fabsf(scales[i][2]));
    }
    /* Screen initialization must not multiply stale NaNs/Infs by zero. */
    for(size_t i = 0; i < sizeof(matrix) / sizeof(float); ++i)
        ((float *)&matrix)[i] = i & 1 ? INFINITY : NAN;
    shz_xmtrx_load_4x4(&matrix);
    shz_xmtrx_init_screen(640, 480);
    shz_xmtrx_store_4x4(&observed);
    const float screen[] = {
        320, 0, 0, 0, 0, -240, 0, 0, 0, 0, 1, 0, 320, 240, 0, 1
    };
    for(size_t i = 0; i < 16; ++i)
        passed &= isfinite(((float *)&observed)[i]) &&
                  close_enough(((float *)&observed)[i], screen[i]);
    for(size_t i = 0; i < sizeof(angles) / sizeof(*angles); ++i) {
        shz_sincos_t pair = shz_sincosu16(angles[i]);
        passed &= isfinite(pair.sin) && isfinite(pair.cos) &&
                  close_enough(pair.sin, expected_sin[i]) &&
                  close_enough(pair.cos, expected_cos[i]);
    }
    shz_xmtrx_load_4x4(&saved);
    return passed;
}

#endif
