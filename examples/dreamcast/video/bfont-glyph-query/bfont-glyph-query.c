/* KallistiOS ##version##

   Validate the encoding-aware Dreamcast Boot ROM glyph API.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/biosfont.h>
#include <dc/syscalls.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static int failures;

#define CHECK(condition, ...) do { \
    if(!(condition)) { \
        ++failures; \
        printf("FAIL: " __VA_ARGS__); \
        printf("\n"); \
    } \
} while(0)

static void check_glyph(bfont_glyph_set_t set, uint32_t code,
                        uint32_t expected_offset, uint16_t expected_width,
                        uint16_t expected_height, uint16_t expected_size) {
    bfont_glyph_t glyph;
    uint8_t *base = syscall_font_address();

    CHECK(bfont_lookup_glyph(set, code, &glyph),
          "set %u code 0x%08" PRIx32 " was rejected", set, code);

    if(!bfont_lookup_glyph(set, code, &glyph))
        return;

    CHECK(glyph.offset == expected_offset,
          "set %u code 0x%08" PRIx32 " offset %" PRIu32 " != %" PRIu32,
          set, code, glyph.offset, expected_offset);
    CHECK(glyph.data == base + expected_offset,
          "set %u code 0x%08" PRIx32 " returned the wrong address", set,
          code);
    CHECK(glyph.width == expected_width && glyph.height == expected_height,
          "set %u code 0x%08" PRIx32 " dimensions %ux%u != %ux%u", set,
          code, glyph.width, glyph.height, expected_width, expected_height);
    CHECK(glyph.data_size == expected_size,
          "set %u code 0x%08" PRIx32 " size %u != %u", set, code,
          glyph.data_size, expected_size);
}

int main(void) {
    bfont_glyph_t glyph;
    uint16_t jis = 0;
    volatile uint8_t rom_probe;
    unsigned int attempts;

    printf("Boot ROM glyph query validation\n");

    CHECK(bfont_sjis_to_jis(0x8140, &jis) && jis == 0x2121,
          "Shift-JIS 0x8140 did not convert to JIS 0x2121");
    CHECK(bfont_sjis_to_jis(0x889f, &jis) && jis == 0x3021,
          "Shift-JIS 0x889f did not convert to JIS 0x3021");
    CHECK(bfont_sjis_to_jis(0xeaa4, &jis) && jis == 0x7426,
          "Shift-JIS 0xeaa4 did not convert to JIS 0x7426");
    CHECK(!bfont_sjis_to_jis(0x817f, &jis),
          "invalid Shift-JIS trail byte was accepted");
    CHECK(!bfont_sjis_to_jis(0x0041, &jis),
          "single-byte input was accepted as two-byte Shift-JIS");
    CHECK(!bfont_sjis_to_jis(0x8140, NULL),
          "NULL JIS output was accepted");

    check_glyph(BFONT_GLYPH_ISO8859_1, 0x20, BFONT_BLANK,
                BFONT_THIN_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_CHAR);
    CHECK(BFONT_BLANK != BFONT_OVERBAR,
          "blank space aliases the BIOS overbar glyph");
    check_glyph(BFONT_GLYPH_ISO8859_1, 0xa0,
                BFONT_ISO_8859_1_160_255, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                BFONT_BYTES_PER_CHAR);
    check_glyph(BFONT_GLYPH_JISX0201, 0x5c, BFONT_YEN,
                BFONT_THIN_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_CHAR);
    check_glyph(BFONT_GLYPH_JISX0201, 0xa1,
                BFONT_JISX_0201_160_255 + BFONT_BYTES_PER_CHAR,
                BFONT_THIN_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_CHAR);
    check_glyph(BFONT_GLYPH_JISX0208, 0x2121, BFONT_WIDE_START,
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_SHIFT_JIS, 0x889f, BFONT_JISX_0208_ROW16,
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_SHIFT_JIS, 0x989f, BFONT_JISX_0208_ROW48,
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_SHIFT_JIS, 0xeaa4,
                BFONT_DREAMCAST_SPECIFIC - BFONT_BYTES_PER_WIDE_CHAR,
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_EUC_JP, 0xa1a1, BFONT_WIDE_START,
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_EUC_JP, 0x8ea1,
                BFONT_JISX_0201_160_255 + BFONT_BYTES_PER_CHAR,
                BFONT_THIN_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_CHAR);
    check_glyph(BFONT_GLYPH_DREAMCAST_ICON,
                BFONT_DREAMCAST_ICON_COUNT - 1,
                BFONT_DC_ICON(BFONT_DREAMCAST_ICON_COUNT - 1),
                BFONT_WIDE_WIDTH, BFONT_HEIGHT, BFONT_BYTES_PER_WIDE_CHAR);
    check_glyph(BFONT_GLYPH_VMU_ICON, BFONT_VMU_ICON_COUNT - 1,
                BFONT_VMU_DREAMCAST_SPECIFIC +
                (BFONT_VMU_ICON_COUNT - 1) * BFONT_ICON_DIMEN *
                BFONT_ICON_DIMEN / 8,
                BFONT_ICON_DIMEN, BFONT_ICON_DIMEN,
                BFONT_ICON_DIMEN * BFONT_ICON_DIMEN / 8);

    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_ISO8859_1, 0x7f, &glyph),
          "ISO-8859-1 control code was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_JISX0208, 0x2821, &glyph),
          "missing JIS row 8 was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_SHIFT_JIS, 0x8397, &glyph),
          "first missing Shift-JIS gap code was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_SHIFT_JIS, 0x889e, &glyph),
          "last missing Shift-JIS gap code was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_JISX0208, 0x7427, &glyph),
          "unpopulated JIS row-84 cell was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_DREAMCAST_ICON,
                              BFONT_DREAMCAST_ICON_COUNT, &glyph),
          "out-of-range Dreamcast icon was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_VMU_ICON, BFONT_VMU_ICON_COUNT,
                              &glyph),
          "out-of-range VMU icon was accepted");
    CHECK(!bfont_lookup_glyph(BFONT_GLYPH_ISO8859_1, 0x20, NULL),
          "NULL glyph output was accepted");

    for(attempts = 0; attempts < 1000; ++attempts) {
        if(syscall_font_lock() == 0)
            break;
        thd_pass();
    }

    CHECK(attempts != 1000, "could not acquire the Boot ROM font semaphore");
    if(attempts != 1000) {
        CHECK(bfont_lookup_glyph(BFONT_GLYPH_VMU_ICON,
                                 BFONT_ICON_HOURGLASS_ONE, &glyph),
              "could not locate a VMU icon for the ROM probe");
        rom_probe = glyph.data[0] ^ glyph.data[glyph.data_size - 1];
        (void)rom_probe;
        syscall_font_unlock();
    }

    if(failures) {
        printf("RESULT: FAIL (%d checks)\n", failures);
        return 1;
    }

    printf("RESULT: PASS\n");
    return 0;
}
