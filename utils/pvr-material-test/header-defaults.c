/* KallistiOS ##version##
   Target-side packet regression: uses the real PVR header compilers.
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>

#define CHECK(test) do { \
    if(!(test)) { \
        printf("header-defaults: line %d failed: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while(0)

int main(void) {
    static const pvr_list_t lists[] = {
        PVR_LIST_OP_POLY, PVR_LIST_TR_POLY, PVR_LIST_PT_POLY
    };
    static const pvr_clip_mode_t clips[] = {
        PVR_USERCLIP_DISABLE, PVR_USERCLIP_INSIDE, PVR_USERCLIP_OUTSIDE
    };
    static const pvr_strip_len_t strips[] = {
        PVR_STRIP_LEN_1, PVR_STRIP_LEN_2, PVR_STRIP_LEN_4, PVR_STRIP_LEN_6
    };
    pvr_poly_cxt_t context;
    pvr_sprite_cxt_t sprite_context;
    pvr_poly_hdr_t polygon;
    pvr_poly_mod_hdr_t two_volume;
    pvr_sprite_hdr_t sprite;
    pvr_mod_hdr_t modifier;

    CHECK(PVR_STRIP_LEN_6 == 3);
    CHECK(PVR_CMD_POLYHDR == UINT32_C(0x808c0000));
    for(size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
        for(size_t j = 0; j < sizeof(clips) / sizeof(clips[0]); ++j) {
            for(unsigned kind = 0; kind < 4; ++kind) {
                const bool textured = kind & 1;
                const bool modified = kind & 2;
                pvr_ptr_t texture = (pvr_ptr_t)UINT32_C(0xa4000000);
                if(kind == 0)
                    pvr_poly_cxt_col(&context, lists[i]);
                else if(kind == 1)
                    pvr_poly_cxt_txr(&context, lists[i], PVR_TXRFMT_RGB565,
                                    8, 8, texture, PVR_FILTER_NONE);
                else if(kind == 2)
                    pvr_poly_cxt_col_mod(&context, lists[i]);
                else
                    pvr_poly_cxt_txr_mod(&context, lists[i], PVR_TXRFMT_RGB565,
                        8, 8, texture, PVR_FILTER_NONE, PVR_TXRFMT_RGB565,
                        8, 8, texture, PVR_FILTER_NONE);
                CHECK(context.gen.strip_len == PVR_STRIP_LEN_6);
                context.gen.clip_mode = clips[j];
                /* First compile the default, then every explicit choice. */
                for(size_t k = 0; k <= sizeof(strips) / sizeof(strips[0]); ++k) {
                    uint32_t command;
                    pvr_strip_len_t expected = k ? strips[k - 1] : PVR_STRIP_LEN_6;
                    if(k)
                        context.gen.strip_len = expected;
                    if(modified) {
                        pvr_poly_mod_compile(&two_volume, &context);
                        command = two_volume.cmd;
                    } else {
                        pvr_poly_compile(&polygon, &context);
                        command = polygon.cmd;
                    }
                    CHECK(((command >> 18) & 3) == (uint32_t)expected);
                    CHECK(command & (1u << 23));
                    CHECK(((command >> 24) & 7) == (uint32_t)lists[i]);
                    CHECK(((command >> 16) & 3) == (uint32_t)clips[j]);
                    CHECK(!!(command & (1u << 3)) == textured);
                }
            }
        }
    }

    pvr_sprite_cxt_col(&sprite_context, PVR_LIST_OP_POLY);
    pvr_sprite_compile(&sprite, &sprite_context);
    CHECK((sprite.cmd & UINT32_C(0x808c0000)) == PVR_CMD_MODIFIER);
    pvr_mod_compile(&modifier, PVR_LIST_OP_MOD, PVR_MODIFIER_INCLUDE_LAST_POLY,
                    PVR_CULLING_NONE);
    CHECK((modifier.cmd & UINT32_C(0x808c0000)) == PVR_CMD_MODIFIER);
    puts("header-defaults: PASS (packet encoding only, no rendering)");
    return 0;
}
