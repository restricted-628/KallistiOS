/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black

   KOS-side regression probes for the pinned SH4ZAM dependency.
*/
#ifndef SH4ZAM_RELEASE_FIXTURES_H
#define SH4ZAM_RELEASE_FIXTURES_H

/* Independent scalar Hamilton product, used only as a regression oracle. */
static shz_quat_t release_quat_product(shz_quat_t a, shz_quat_t b) {
    return shz_quat_init(a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
                        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w);
}

static shz_quat_t release_quat_oracle(unsigned order, float x, float y, float z) {
    shz_quat_t q[] = {
        shz_quat_init(cosf(x/2), sinf(x/2), 0, 0),
        shz_quat_init(cosf(y/2), 0, sinf(y/2), 0),
        shz_quat_init(cosf(z/2), 0, 0, sinf(z/2))
    };
    static const unsigned axes[][3] = {{0,1,2}, {2,1,0}, {2,0,1}, {1,0,2}};
    return release_quat_product(release_quat_product(q[axes[order][0]],
                                                    q[axes[order][1]]),
                                q[axes[order][2]]);
}

/* q and -q describe the same orientation. */
static bool release_quat_matches(shz_quat_t a, shz_quat_t b, float tolerance) {
    float sign = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z < 0 ? -1 : 1;
    return fabsf(a.w - sign*b.w) <= tolerance &&
           fabsf(a.x - sign*b.x) <= tolerance &&
           fabsf(a.y - sign*b.y) <= tolerance &&
           fabsf(a.z - sign*b.z) <= tolerance;
}

/* Keep runtime FSCA inputs and extraction live, including under optimization. */
__attribute__((noinline)) static shz_quat_t
release_quat_call(unsigned order, float x, float y, float z) {
    switch(order) {
        case 0: return shz_quat_from_rotation_xyz(x,y,z);
        case 1: return shz_quat_from_rotation_zyx(z,y,x);
        case 2: return shz_quat_from_rotation_zxy(z,x,y);
        default: return shz_quat_from_rotation_yxz(y,x,z);
    }
}

__attribute__((noinline)) static shz_quat_t
release_quat_u16_call(unsigned order, uint16_t x, uint16_t y, uint16_t z) {
    switch(order) {
        case 0: return shz_quat_from_rotation_xyz_u16(x,y,z);
        case 1: return shz_quat_from_rotation_zyx_u16(z,y,x);
        case 2: return shz_quat_from_rotation_zxy_u16(z,x,y);
        default: return shz_quat_from_rotation_yxz_u16(y,x,z);
    }
}

__attribute__((noinline)) static shz_vec3_t release_quat_angles(shz_quat_t q) {
    return shz_quat_to_angles_xyz(q);
}

static bool verify_release_quaternion(void) {
    static const float inputs[][3] = {
        {0,0,0}, {.3f,-.7f,1.1f}, {-2.4f,.9f,-1.8f},
        {.4f,SHZ_F_PI/2,-.8f}, {.4f,-SHZ_F_PI/2,-.8f},
        {.4f,SHZ_F_PI/2-.01f,-.8f}, {.4f,-SHZ_F_PI/2+.01f,-.8f},
        {0,0,2.5f}, {0,0,-2.5f}
    };
    static const uint16_t binary[][3] = {
        {0,0,0}, {16384,32768,49152}, {65535,1,32767}, {4321,54321,23456}
    };
    bool passed = true;
    for(unsigned i = 0; i < sizeof(inputs)/sizeof(*inputs); ++i) {
        float x = inputs[i][0], y = inputs[i][1], z = inputs[i][2];
        for(unsigned order = 0; order < 4; ++order) {
            shz_quat_t q = release_quat_call(order,x,y,z);
            bool ok = release_quat_matches(q, release_quat_oracle(order,x,y,z), 0.002f);
            if(order == 0) {
                shz_vec3_t angles = release_quat_angles(q);
                /* Extraction uses approximate asin/atan2 (upstream tests use
                   0.03 radians). Keep constructor checks separately tighter. */
                bool roundtrip = release_quat_matches(q, release_quat_oracle(0,angles.x,
                                                                 angles.y,angles.z), 0.01f);
                if(!roundtrip)
                    printf("SH4ZAM Euler roundtrip: in=(%g,%g,%g) out=(%g,%g,%g)\n",
                           (double)x,(double)y,(double)z,
                           (double)angles.x,(double)angles.y,(double)angles.z);
                ok &= roundtrip;
                if(x == 0 && y == 0)
                    ok &= fabsf(shz_quat_angle_z(q) - z) <= 0.03f;
            }
            if(!ok)
                printf("SH4ZAM quaternion mismatch: input=%u order=%u\n", i, order);
            passed &= ok;
        }
    }
    for(unsigned i = 0; i < sizeof(binary)/sizeof(*binary); ++i) {
        float radians[3];
        /* Odd u16 inputs round their signed half-angle down by half a unit. */
        for(unsigned axis = 0; axis < 3; ++axis)
            radians[axis] = floorf((float)(int16_t)binary[i][axis]/2) *
                            (SHZ_F_TAU/32768.0f);
        for(unsigned order = 0; order < 4; ++order) {
            bool ok = release_quat_matches(
                release_quat_u16_call(order,binary[i][0],binary[i][1],binary[i][2]),
                release_quat_oracle(order,radians[0],radians[1],radians[2]), 0.002f);
            if(!ok)
                printf("SH4ZAM u16 quaternion mismatch: input=%u order=%u\n", i, order);
            passed &= ok;
        }
    }
    return passed;
}

/* Raw float buffers must not inherit the native matrix's vector alignment.
   Exercise every offset modulo 16 that is valid for a float buffer. */
static bool verify_release_alignment(void) {
    float input[20] __attribute__((aligned(16)));
    float output[20] __attribute__((aligned(16)));
    shz_mat4x4_t a, b, saved;
    bool passed = true;

    _Static_assert(__alignof__(shz_mat4x4_t) ==
                   __alignof__(struct shz_mat4x4), "matrix type alignment");
    _Static_assert(sizeof(shz_mat4x4_t) == 16 * sizeof(float), "matrix size");
    for(unsigned i = 0; i < 16; ++i) {
        a.elem[i] = (float)((int)(i * 3 % 11) - 5);
        b.elem[i] = (float)((int)(i * 7 % 13) - 6);
    }
    shz_xmtrx_store_4x4(&saved);
    for(unsigned offset = 0; offset < 4; ++offset) {
        for(unsigned i = 0; i < 20; ++i)
            input[i] = -999;
        memcpy(input + offset, b.elem, sizeof(b));
        for(unsigned op = 0; op < 8; ++op) {
            float expected[16];
            for(unsigned col = 0; col < 4; ++col) {
                for(unsigned row = 0; row < 4; ++row) {
                    unsigned i = col * 4 + row;
                    if(op < 2)
                        expected[i] = b.elem[op ? row * 4 + col : i];
                    else if(op >= 6)
                        expected[i] = a.elem[op == 7 ? row * 4 + col : i];
                    else {
                        float sum = 0;
                        for(unsigned k = 0; k < 4; ++k) {
                            float left = op < 4 ? a.elem[k * 4 + row] :
                                b.elem[op == 5 ? row * 4 + k : k * 4 + row];
                            float right = op >= 4 ? a.elem[col * 4 + k] :
                                b.elem[op == 3 ? k * 4 + col : col * 4 + k];
                            sum += left * right;
                        }
                        expected[i] = sum;
                    }
                }
            }
            for(unsigned i = 0; i < 20; ++i)
                output[i] = -999;
            shz_xmtrx_load_4x4(&a);
            switch(op) {
                case 0: shz_xmtrx_load_unaligned_4x4(input + offset); break;
                case 1: shz_xmtrx_load_transpose_unaligned_4x4(input + offset); break;
                case 2: shz_xmtrx_apply_unaligned_4x4(input + offset); break;
                case 3: shz_xmtrx_apply_transpose_unaligned_4x4(input + offset); break;
                case 4: shz_xmtrx_apply_reverse_unaligned_4x4(input + offset); break;
                case 5: shz_xmtrx_apply_reverse_transpose_unaligned_4x4(input + offset); break;
                default: break;
            }
            if(op == 7)
                shz_xmtrx_store_transpose_unaligned_4x4(output + offset);
            else
                shz_xmtrx_store_unaligned_4x4(output + offset);
            for(unsigned i = 0; i < 20; ++i) {
                bool inside = i >= offset && i < offset + 16;
                float want = inside ? expected[i - offset] : -999;
                float source = inside ? b.elem[i - offset] : -999;
                if(!close_enough(output[i], want) || input[i] != source) {
                    printf("SH4ZAM alignment mismatch: offset=%u op=%u index=%u\n",
                           offset, op, i);
                    passed = false;
                }
            }
        }
    }
    shz_xmtrx_load_4x4(&saved);
    return passed;
}

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
    /* 0.9.0: non-affine W lanes must not contribute to XYZ scale lengths. */
    shz_mat4x4_init_scale(&matrix, 2, 3, 4);
    matrix.elem[3] = 7;
    matrix.elem[7] = 11;
    matrix.elem[11] = 13;
    shz_vec3_t scale = shz_mat4x4_get_scale(&matrix);
    shz_xmtrx_load_4x4(&matrix);
    shz_vec3_t resident_scale = shz_xmtrx_get_scale();
    passed &= close_enough(scale.x, 2) && close_enough(scale.y, 3) &&
              close_enough(scale.z, 4) && close_enough(resident_scale.x, 2) &&
              close_enough(resident_scale.y, 3) && close_enough(resident_scale.z, 4);
    shz_vec3_t vector = shz_mat4x4_transform_vec3(&matrix, shz_vec3_init(1, 2, 3));
    passed &= close_enough(vector.x, 2) && close_enough(vector.y, 6) &&
              close_enough(vector.z, 12);
    shz_xmtrx_store_4x4(&observed);
    passed &= memcmp(&matrix, &observed, sizeof(matrix)) == 0;
    shz_xmtrx_load_4x4(&saved);
    return passed;
}

/* Independent scalar oracle: column-major 4x4 product, no SH4ZAM math. */
static void release_product(float out[16], const float a[16], const float b[16]) {
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 4; ++r) {
            out[c * 4 + r] = 0;
            for(unsigned k = 0; k < 4; ++k)
                out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
        }
}

static bool release_xmtrx_matches(const float expected[16]) {
    shz_mat4x4_t actual;
    shz_xmtrx_store_4x4(&actual);
    for(unsigned i = 0; i < 16; ++i)
        if(!isfinite(actual.elem[i]) || !close_enough(actual.elem[i], expected[i])) {
            printf("SH4ZAM 3x4 mismatch at %u: %.9g expected %.9g\n",
                   i, (double)actual.elem[i], (double)expected[i]);
            return false;
        }
    return true;
}

static bool verify_release_3x4(void) {
    shz_mat3x4_t a __attribute__((aligned(32)));
    shz_mat3x4_t b __attribute__((aligned(32)));
    shz_mat3x4_t row_b __attribute__((aligned(32)));
    shz_mat3x4_t stored __attribute__((aligned(32)));
    shz_mat4x4_t saved;
    float wide_a[16] = { 0 }, wide_b[16] = { 0 }, expected[16];
    bool passed = true;

    shz_xmtrx_store_4x4(&saved);
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 3; ++r) {
            a.elem[c * 3 + r] = (float)(1 + c * 3 + r);
            b.elem[c * 3 + r] = (float)((int)c * 2 - (int)r * 3 + 2);
            row_b.elem[r * 4 + c] = b.elem[c * 3 + r];
            wide_a[c * 4 + r] = a.elem[c * 3 + r];
            wide_b[c * 4 + r] = b.elem[c * 3 + r];
        }
    wide_a[15] = wide_b[15] = 1;
    shz_xmtrx_load_3x4(&b);
    passed &= release_xmtrx_matches(wide_b);
    shz_xmtrx_store_transpose_3x4(&stored);
    for(unsigned i = 0; i < 12; ++i)
        passed &= close_enough(stored.elem[i], row_b.elem[i]);
    shz_xmtrx_load_transpose_3x4(&row_b);
    passed &= release_xmtrx_matches(wide_b);
    shz_xmtrx_load_cols_3x4(&b.col[0], &b.col[1], &b.col[2], &b.col[3]);
    passed &= release_xmtrx_matches(wide_b);

    release_product(expected, wide_a, wide_b);
    shz_xmtrx_load_3x4(&a);
    shz_xmtrx_apply_3x4(&b);
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_load_3x4(&a);
    shz_xmtrx_apply_transpose_3x4(&row_b);
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_load_apply_3x4(&a, &b);
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_load_apply_store_3x4(&stored, &a, &b);
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 3; ++r)
            passed &= close_enough(stored.elem[c * 3 + r], expected[c * 4 + r]);

    release_product(expected, wide_b, wide_a);
    shz_xmtrx_load_3x4(&a);
    shz_xmtrx_apply_reverse_3x4(&b);
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_load_3x4(&a);
    shz_xmtrx_apply_reverse_transpose_3x4(&row_b);
    passed &= release_xmtrx_matches(expected);

    /* Weighted affine accumulation must preserve every fourth-row lane. */
    shz_mat4x4_t start;
    for(unsigned i = 0; i < 16; ++i)
        start.elem[i] = expected[i] = (float)((int)i - 5);
    shz_xmtrx_load_4x4(&start);
    shz_xmtrx_blend_3x4(&a, 0.25f);
    shz_xmtrx_blend_3x4(&b, -0.5f);
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 3; ++r)
            expected[c * 4 + r] += a.elem[c * 3 + r] * 0.25f -
                                   b.elem[c * 3 + r] * 0.5f;
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_blend_3x4(&a, 0.0f);
    passed &= release_xmtrx_matches(expected);

    /* The explicitly named full-matrix operation updates all sixteen lanes. */
    shz_xmtrx_blend_4x4(&start, 0.5f);
    for(unsigned i = 0; i < 16; ++i)
        expected[i] += start.elem[i] * 0.5f;
    passed &= release_xmtrx_matches(expected);
    shz_xmtrx_load_4x4(&saved);
    return passed;
}

#endif
