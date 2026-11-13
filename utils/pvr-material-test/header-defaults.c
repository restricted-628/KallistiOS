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
            pvr_poly_cxt_col(&context, lists[i]);
            context.gen.clip_mode = clips[j];
            pvr_poly_compile(&polygon, &context);
            CHECK(((polygon.cmd >> 18) & 3) == PVR_STRIP_LEN_6);
            CHECK(polygon.cmd & (1u << 23));
            CHECK(((polygon.cmd >> 24) & 7) == (uint32_t)lists[i]);
            CHECK(((polygon.cmd >> 16) & 3) == clips[j]);

            pvr_poly_cxt_col_mod(&context, lists[i]);
            context.gen.clip_mode = clips[j];
            pvr_poly_mod_compile(&two_volume, &context);
            CHECK(((two_volume.cmd >> 18) & 3) == PVR_STRIP_LEN_6);
            CHECK(two_volume.cmd & (1u << 23));
            CHECK(((two_volume.cmd >> 24) & 7) == (uint32_t)lists[i]);
            CHECK(((two_volume.cmd >> 16) & 3) == clips[j]);
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
