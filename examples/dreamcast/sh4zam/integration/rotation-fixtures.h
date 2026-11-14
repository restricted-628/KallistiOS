/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Runtime regressions for the combined XMTRX rotation and permutation APIs.
   Include after release-fixtures.h for its independent Hamilton oracle. */
#ifndef SH4ZAM_ROTATION_FIXTURES_H
#define SH4ZAM_ROTATION_FIXTURES_H

static uint32_t rotation_control(void) {
#if SHZ_BACKEND == SHZ_SH4
    uint32_t fpscr;
    __asm__ volatile("sts fpscr,%0" : "=r"(fpscr));
    /* PR, SZ, DN and rounding mode; bank selection and arithmetic flags
       are not promised unchanged by every XMTRX operation. */
    return fpscr & UINT32_C(0x001c0003);
#else
    return 0;
#endif
}

static void rotation_matrix_oracle(shz_mat4x4_t *m, shz_quat_t q) {
    double x = q.x, y = q.y, z = q.z, w = q.w;
    const float a[16] = {
        1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y), 0,
        2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x), 0,
        2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y), 0,
        0, 0, 0, 1
    };
    memcpy(m->elem, a, sizeof(a));
}

static void rotation_product_oracle(shz_mat4x4_t *out,
                                    const shz_mat4x4_t *a,
                                    const shz_mat4x4_t *b) {
    for(unsigned c = 0; c < 4; ++c)
        for(unsigned r = 0; r < 4; ++r) {
            double sum = 0;
            for(unsigned k = 0; k < 4; ++k)
                sum += (double)a->elem[k*4+r] * b->elem[c*4+k];
            out->elem[c*4+r] = (float)sum;
        }
}

static bool rotation_matches(const shz_mat4x4_t *a, const shz_mat4x4_t *b) {
    for(unsigned i = 0; i < 16; ++i)
        if(!isfinite(a->elem[i]) || fabsf(a->elem[i]-b->elem[i]) > .002f)
            return false;
    return true;
}

__attribute__((noinline)) static void
rotation_euler_call(unsigned order, unsigned mode, float x, float y, float z,
                    uint16_t ux, uint16_t uy, uint16_t uz) {
    if(mode == 0) {
        switch(order) {
            case 0: shz_xmtrx_init_rotation_xyz(x,y,z); break;
            case 1: shz_xmtrx_init_rotation_zyx(z,y,x); break;
            case 2: shz_xmtrx_init_rotation_zxy(z,x,y); break;
            default: shz_xmtrx_init_rotation_yxz(y,x,z); break;
        }
    } else if(mode == 1) {
        switch(order) {
            case 0: shz_xmtrx_rotate_xyz(x,y,z); break;
            case 1: shz_xmtrx_rotate_zyx(z,y,x); break;
            case 2: shz_xmtrx_rotate_zxy(z,x,y); break;
            default: shz_xmtrx_rotate_yxz(y,x,z); break;
        }
    } else {
        switch(order) {
            case 0: shz_xmtrx_rotate_xyz_u16(ux,uy,uz); break;
            case 1: shz_xmtrx_rotate_zyx_u16(uz,uy,ux); break;
            case 2: shz_xmtrx_rotate_zxy_u16(uz,ux,uy); break;
            default: shz_xmtrx_rotate_yxz_u16(uy,ux,uz); break;
        }
    }
}

static bool verify_release_rotations(void) {
    const uint32_t control = rotation_control();
    static const uint16_t angles[][3] = {
        {0,0,0}, {0x4000,0,0}, {0,0x8000,0}, {0,0,0xc000},
        {3129,58235,11473}, {62501,9311,39719}, {65535,1,32769}
    };
    const shz_mat4x4_t base = {.elem = {
        2,.2f,-.3f,.1f, -.4f,1.5f,.6f,-.2f,
        .1f,-.2f,.75f,.3f, 5,-3,7,1
    }};
    for(unsigned n = 0; n < sizeof(angles)/sizeof(angles[0]); ++n) {
        float x = angles[n][0]*(SHZ_F_PI/32768.0f);
        float y = angles[n][1]*(SHZ_F_PI/32768.0f);
        float z = angles[n][2]*(SHZ_F_PI/32768.0f);
        for(unsigned order = 0; order < 4; ++order) {
            shz_mat4x4_t expected, product, actual;
            shz_quat_t q = release_quat_oracle(order, x, y, z);
            rotation_matrix_oracle(&expected, q);
            rotation_product_oracle(&product, &base, &expected);
            for(unsigned mode = 0; mode < 3; ++mode) {
                shz_xmtrx_load_4x4(&base);
                rotation_euler_call(order, mode, x,y,z,
                                    angles[n][0],angles[n][1],angles[n][2]);
                shz_xmtrx_store_4x4(&actual);
                if(rotation_control() != control) return false;
                if(!rotation_matches(&actual, mode ? &product : &expected))
                    return false;
            }
            shz_xmtrx_load_4x4(&base);
            shz_xmtrx_init_rotation_quat(q);
            shz_xmtrx_store_4x4(&actual);
            if(rotation_control() != control) return false;
            if(!rotation_matches(&actual, &expected)) return false;
            shz_xmtrx_load_4x4(&base);
            shz_xmtrx_rotate_quat(q);
            shz_xmtrx_store_4x4(&actual);
            if(rotation_control() != control) return false;
            if(!rotation_matches(&actual, &product)) return false;
        }
        for(unsigned axis = 0; axis < 3; ++axis) {
            shz_mat4x4_t expected, product, actual;
            rotation_matrix_oracle(&expected, release_quat_oracle(0,
                axis == 0 ? x : 0, axis == 1 ? y : 0, axis == 2 ? z : 0));
            rotation_product_oracle(&product, &base, &expected);
            shz_xmtrx_load_4x4(&base);
            if(axis == 0) shz_xmtrx_rotate_x_u16(angles[n][0]);
            else if(axis == 1) shz_xmtrx_rotate_y_u16(angles[n][1]);
            else shz_xmtrx_rotate_z_u16(angles[n][2]);
            shz_xmtrx_store_4x4(&actual);
            if(rotation_control() != control ||
               !rotation_matches(&actual, &product)) return false;
        }
    }
    return true;
}

static bool verify_release_permutations(void) {
    const uint32_t control = rotation_control();
    static const unsigned columns[3][4] = {{1,2,3,0}, {3,0,1,2}, {3,2,1,0}};
    shz_mat4x4_t input, output;
    for(unsigned i = 0; i < 16; ++i) input.elem[i] = (float)(i+1);
    for(unsigned pass = 0; pass < 2; ++pass) {
#if SHZ_BACKEND == SHZ_SH4
        /* Native permutations are register moves, including special bit patterns.
           The software backend still multiplies, so only test finite values there. */
        if(pass) {
            const uint32_t bits[16] = {
                0,0x80000000,1,0x80000001,0x7f800000,0xff800000,0x7fc12345,0xffc12345,
                0x3f800000,0xbf800000,0x00800000,0x80800000,
                0x7f7fffff,0xff7fffff,0x12345678,0x87654321
            };
            memcpy(input.elem, bits, sizeof(bits));
        }
#else
        if(pass) break;
#endif
        for(unsigned op = 0; op < 3; ++op) {
            shz_xmtrx_load_4x4(&input);
            if(op == 0) shz_xmtrx_permute_wxyz();
            else if(op == 1) shz_xmtrx_permute_yzwx();
            else shz_xmtrx_permute_wzyx();
            shz_xmtrx_store_4x4(&output);
            if(rotation_control() != control) return false;
            for(unsigned c = 0; c < 4; ++c)
                if(memcmp(output.elem+c*4, input.elem+columns[op][c]*4,
                          4*sizeof(float))) return false;
        }
    }
    return true;
}
#endif
