/* KallistiOS ##version##

   Recipe admission, packet fields and independent RGBA composition checks.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_material.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef __DREAMCAST__
/* Host tests capture the subset of packet fields used by the composition
   oracle. The target build instead exercises the actual KOS header encoder. */
void pvr_poly_compile_ex(pvr_poly_hdr_t *header,
                         const pvr_poly_cxt_t *c, uint32_t flags) {
    uint32_t words[8] = { 0 };
    (void)flags;
    words[1] = ((uint32_t)c->depth.comparison << 29) |
               ((uint32_t)c->depth.write << 26);
    words[2] = ((uint32_t)c->blend.src << 29) |
               ((uint32_t)c->blend.dst << 26) |
               ((uint32_t)c->blend.src_enable << 25) |
               ((uint32_t)c->blend.dst_enable << 24) |
               ((uint32_t)c->gen.alpha << 20) |
               ((uint32_t)c->txr.filter << 13) |
               ((uint32_t)c->txr.env << 6);
    memcpy(header, words, sizeof(words));
}
void pvr_sprite_compile_ex(pvr_sprite_hdr_t *h,
                           const pvr_sprite_cxt_t *c, uint32_t f) {
    (void)h; (void)c; (void)f; assert(0);
}
void pvr_poly_mod_compile_ex(pvr_poly_mod_hdr_t *h,
                             const pvr_poly_cxt_t *c, uint32_t f) {
    (void)h; (void)c; (void)f; assert(0);
}
int pvr_prim(const void *p, size_t n) {
    (void)p; (void)n; assert(0); return -1;
}
int pvr_list_prim(pvr_list_t l, const void *p, size_t n) {
    (void)l; (void)p; (void)n; assert(0); return -1;
}
#endif

static uint32_t word(const pvr_material_recipe_t *r, size_t pass, size_t index) {
    uint32_t words[8];
    memcpy(words, &r->passes[pass].material.header, sizeof(words));
    return words[index];
}

static pvr_poly_cxt_t surface(bool translucent) {
    pvr_poly_cxt_t c = { 0 };
    c.list_type = translucent ? PVR_LIST_TR_POLY : PVR_LIST_OP_POLY;
    c.gen.alpha = translucent;
    c.gen.shading = true;
    c.gen.fog_type = PVR_FOG_DISABLE;
    c.gen.culling = PVR_CULLING_NONE;
    c.fmt.color = PVR_CLRFMT_ARGBPACKED;
    c.blend.src = translucent ? PVR_BLEND_SRCALPHA : PVR_BLEND_ONE;
    c.blend.dst = translucent ? PVR_BLEND_INVSRCALPHA : PVR_BLEND_ZERO;
    c.depth.comparison = PVR_DEPTHCMP_GREATER;
    c.depth.write = PVR_DEPTHWRITE_ENABLE;
    c.txr.enable = true;
    c.txr.mipmap = true;
    c.txr.width = c.txr.height = 64;
    c.txr.base = (pvr_ptr_t)(uintptr_t)0xa4001000;
    c.txr.format = PVR_TXRFMT_RGB565;
    c.txr.filter = PVR_FILTER_BILINEAR;
    c.txr.mipmap_bias = PVR_MIPBIAS_NORMAL;
    c.txr.env = PVR_TXRENV_MODULATEALPHA;
    return c;
}

static float factor(unsigned blend, unsigned channel, const float *src,
                     const float *dst, bool source) {
    switch(blend) {
        case PVR_BLEND_ZERO: return 0;
        case PVR_BLEND_ONE: return 1;
        case PVR_BLEND_DESTCOLOR: return source ? dst[channel] : src[channel];
        case PVR_BLEND_SRCALPHA: return src[3];
        case PVR_BLEND_INVSRCALPHA: return 1 - src[3];
        default: assert(0); return 0;
    }
}

static void blend(const pvr_material_recipe_t *r, size_t pass,
                   const float shaded[4], float primary[4], float secondary[4]) {
    uint32_t mode = word(r, pass, 2);
    const float *src = mode & (1u << 25) ? secondary : shaded;
    float *dst = mode & (1u << 24) ? secondary : primary;
    float result[4];
    for(unsigned i = 0; i < 4; ++i) {
        float value = src[i] * factor(mode >> 29, i, src, dst, true) +
                      dst[i] * factor((mode >> 26) & 7, i, src, dst, false);
        result[i] = fminf(1, fmaxf(0, value));
    }
    memcpy(dst, result, sizeof(result));
}

static void check_depth(const pvr_material_recipe_t *r, bool translucent) {
    assert(r->pass_count == (translucent ? 3u : 2u));
    assert(r->requires_presort);
    for(size_t i = 0; i < r->pass_count; ++i) {
        assert(r->passes[i].material.kind == PVR_MATERIAL_POLYGON);
        assert(r->passes[i].material.list ==
               (!translucent && !i ? PVR_LIST_OP_POLY : PVR_LIST_TR_POLY));
        /* Hardware uses a write-DISABLE bit, despite the context field name. */
        assert(((word(r, i, 1) >> 26) & 1) == (unsigned)(translucent || i != 0));
        assert(word(r, i, 1) >> 29 ==
               (unsigned)(!translucent && i ? PVR_DEPTHCMP_EQUAL :
                                               PVR_DEPTHCMP_GREATER));
    }
}

static void test_trilinear(void) {
    const float a[4] = { .2f, .7f, .1f, .35f };
    const float b[4] = { .8f, .1f, .5f, .85f };
    const float background[4] = { .1f, .2f, .3f, 1 };
    for(unsigned translucent = 0; translucent < 2; ++translucent) {
        pvr_poly_cxt_t c = surface(translucent), saved = c;
        pvr_material_recipe_t r;
        assert(pvr_material_compile_trilinear(&r, &c, 0) == 0);
        assert(!memcmp(&c, &saved, sizeof(c)));
        check_depth(&r, translucent);
        assert(((word(&r, 0, 2) >> 13) & 3) == PVR_FILTER_TRILINEAR1);
        assert(((word(&r, 1, 2) >> 13) & 3) == PVR_FILTER_TRILINEAR2);
        for(unsigned q = 0; q <= 4; ++q) {
            float t = q * .25f, first[4], second[4], combined[4];
            float primary[4], secondary[4] = { .9f, .4f, .7f, .2f };
            memcpy(primary, background, sizeof(primary));
            for(unsigned i = 0; i < 4; ++i) {
                first[i] = a[i] * (1 - t);
                second[i] = b[i] * t;
                combined[i] = first[i] + second[i];
            }
            blend(&r, 0, first, primary, secondary);
            blend(&r, 1, second, primary, secondary);
            if(translucent)
                blend(&r, 2, background, primary, secondary);
            for(unsigned i = 0; i < 4; ++i) {
                float expected = translucent ? combined[i] * combined[3] +
                    background[i] * (1 - combined[3]) : combined[i];
                assert(fabsf(primary[i] - expected) < 0.00001f);
            }
        }
    }
}

static void test_bump(void) {
    const float color[4] = { .6f, .2f, .8f, .4f };
    const float background[4] = { .2f, .5f, .1f, 1 };
    for(unsigned translucent = 0; translucent < 2; ++translucent) {
        pvr_poly_cxt_t c = surface(translucent), bump = c;
        pvr_material_recipe_t r;
        bump.txr.format = PVR_TXRFMT_BUMP;
        bump.txr.mipmap = false;
        assert(pvr_material_compile_bump(&r, &c, &bump, 0) == 0);
        check_depth(&r, translucent);
        assert(r.passes[0].role == PVR_MATERIAL_PASS_BUMP);
        assert(((word(&r, 0, 2) >> 20) & 1) == 0);
        assert(((word(&r, 0, 2) >> 6) & 3) == PVR_TXRENV_DECAL);
        for(unsigned q = 0; q <= 4; ++q) {
            float h = q * .25f, light[4] = { h, h, h, 1 };
            float primary[4], secondary[4] = { .7f, .3f, .5f, .9f };
            memcpy(primary, background, sizeof(primary));
            blend(&r, 0, light, primary, secondary);
            blend(&r, 1, color, primary, secondary);
            if(translucent) {
                assert(fabsf(secondary[3] - color[3]) < .00001f);
                blend(&r, 2, background, primary, secondary);
            }
            for(unsigned i = 0; i < 3; ++i) {
                float expected = color[i] * h;
                if(translucent)
                    expected = expected * color[3] + background[i] * (1 - color[3]);
                assert(fabsf(primary[i] - expected) < .00001f);
            }
        }
    }
}

static void test_rejection(void) {
    pvr_material_recipe_t r, saved;
    for(unsigned error = 0; error < 10; ++error) {
        pvr_poly_cxt_t c = surface(true);
        memset(&r, 0xa5, sizeof(r));
        memcpy(&saved, &r, sizeof(saved));
        switch(error) {
            case 0: c.list_type = PVR_LIST_PT_POLY; break;
            case 1: c.txr.mipmap = false; break;
            case 2: c.txr.enable = false; break;
            case 3: c.txr.width = 63; break;
            case 4: c.txr.format = PVR_TXRFMT_BUMP; break;
            case 5: c.blend.src_enable = true; break;
            case 6: c.blend.dst_enable = true; break;
            case 7: c.gen.fog_type = PVR_FOG_TABLE; break;
            case 8: c.fmt.uv = true; break;
            case 9: c.gen.color_clamp = true; break;
        }
        assert(pvr_material_compile_trilinear(&r, &c, 0) == -1);
        assert(errno == EINVAL && !memcmp(&r, &saved, sizeof(r)));
    }
    pvr_poly_cxt_t c = surface(true), bump = c;
    assert(pvr_material_compile_bump(&r, &c, &bump, 0) == -1);
    assert(!memcmp(&r, &saved, sizeof(r)));
    bump.txr.format = PVR_TXRFMT_BUMP;
    bump.txr.mipmap = false;
    bump.txr.width = 63;
    assert(pvr_material_compile_bump(&r, &c, &bump, 0) == -1);
    assert(!memcmp(&r, &saved, sizeof(r)));
    assert(pvr_material_compile_trilinear(&r, &c, UINT32_MAX) == -1);
    assert(pvr_material_compile_trilinear(&r, NULL, 0) == -1);
    assert(pvr_material_compile_bump(NULL, &c, &bump, 0) == -1);
    union {
        pvr_material_recipe_t recipe;
        pvr_poly_cxt_t context;
    } overlapping;
    overlapping.context = c;
    assert(pvr_material_compile_trilinear(&overlapping.recipe,
                                          &overlapping.context, 0) == -1);
    assert(errno == EINVAL);
    assert(!memcmp(&overlapping.context, &c, sizeof(c)));
}

int main(void) {
    test_trilinear();
    test_bump();
    test_rejection();
    puts("pvr-material-recipe-test: PASS");
    return 0;
}
