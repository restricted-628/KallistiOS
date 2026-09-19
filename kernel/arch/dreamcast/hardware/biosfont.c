/* KallistiOS ##version##

   biosfont.c

   Copyright (C) 2000-2002 Megan Potter
   Japanese code Copyright (C) Kazuaki Matsumoto
   Copyright (C) 2017, 2024 Donald Haase
   Copyright (C) 2024 Andy Barajas
   Copyright (C) 2024 Falco Girgis
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <dc/video.h>
#include <dc/biosfont.h>
#include <dc/syscalls.h>

#include <kos/dbglog.h>
#include <kos/thread.h>

/*

This module handles interfacing to the bios font. It supports the standard
European encodings via ISO8859-1, and Japanese in both Shift-JIS and EUC
modes. For Windows/Cygwin users, you'll probably want to call
bfont_set_encoding(BFONT_CODE_SJIS) so that your messages are displayed
properly; otherwise it will default to EUC (for *nix).

Thanks to Marcus Comstedt for the bios font information.

All the Japanese code is by Kazuaki Matsumoto.

Foreground/background color switching based on code by Chilly Willy.

Expansion to 4 and 8 bpp by Donald Haase.

*/

/* Our current conversion mode */
static uint8_t bfont_code_mode = BFONT_CODE_ISO8859_1;

/* Current colors/pixel format. Default to white foreground, black background
   and 16-bit drawing, so the default behavior doesn't change from what it has
   been forever. */
static uint32_t bfont_fgcolor = 0xFFFFFFFF;
static uint32_t bfont_bgcolor = 0x00000000;

static uint8_t *font_address = NULL;

static uint8_t *get_font_address(void) {
    if(!font_address)
        font_address = syscall_font_address();

    return font_address;
}

static inline uint8_t bits_per_pixel(void) {
    return ((vid_mode->pm == PM_RGB0888) ? sizeof(uint32_t) : sizeof(uint16_t)) << 3;
}

/* Select an encoding for Japanese (or disable) */
void bfont_set_encoding(bfont_code_t enc) {
    if(enc <= BFONT_CODE_RAW)
        bfont_code_mode = enc;
    else
        assert_msg(0, "Unknown bfont encoding mode");
}

/* Set the foreground color and return the old color */
uint32_t bfont_set_foreground_color(uint32_t c) {
    uint32_t rv = bfont_fgcolor;
    bfont_fgcolor = c;
    return rv;
}

/* Set the background color and return the old color */
uint32_t bfont_set_background_color(uint32_t c) {
    uint32_t rv = bfont_bgcolor;
    bfont_bgcolor = c;
    return rv;
}

static int bfont_lock(void *d) {
    (void)d;

    return syscall_font_lock() == 0;
}

int lock_bfont(void) {
    /* Just make sure no outside system took the lock */
    thd_poll(bfont_lock, NULL, 0);

    return 0;
}

int unlock_bfont(void) {
    syscall_font_unlock();

    return 0;
}

bool bfont_sjis_to_jis(uint16_t sjis, uint16_t *jis) {
    uint8_t lead = sjis >> 8;
    uint8_t trail = sjis;
    uint16_t row;
    uint16_t cell;

    if(!jis || !((lead >= 0x81 && lead <= 0x9f) ||
                 (lead >= 0xe0 && lead <= 0xef)) ||
       !((trail >= 0x40 && trail <= 0x7e) ||
         (trail >= 0x80 && trail <= 0xfc)))
        return false;

    row = (uint16_t)(lead - (lead <= 0x9f ? 0x71 : 0xb1));
    row = (uint16_t)((row << 1) + 1);

    if(trail >= 0x9f) {
        ++row;
        cell = (uint16_t)(trail - 0x7e);
    }
    else {
        if(trail > 0x7f)
            --trail;

        cell = (uint16_t)(trail - 0x1f);
    }

    *jis = (uint16_t)((row << 8) | cell);
    return true;
}


/* EUC -> JIS conversion */
static uint32_t euc2jis(uint32_t euc) {
    return euc & ~0x8080;
}

static bool bfont_jisx0208_offset(uint16_t jis, uint32_t *offset) {
    uint32_t row = jis >> 8;
    uint32_t cell = jis & 0xff;
    uint32_t packed_row;

    if(cell < 0x21 || cell > 0x7e)
        return false;

    /* The ROM contains rows 1-7 and 16-83 in full, followed by the
       first six characters of row 84. Rows 8-15 are not present. */
    if(!((row >= 0x21 && row <= 0x27) ||
         (row >= 0x30 && row <= 0x73) ||
         (row == 0x74 && cell <= 0x26)))
        return false;

    packed_row = row - 0x21;
    if(row >= 0x30)
        packed_row -= 8;

    *offset = BFONT_WIDE_START +
              (packed_row * JISX_0208_ROW_SIZE + cell - 0x21) *
              BFONT_BYTES_PER_WIDE_CHAR;
    return true;
}

static bool bfont_jisx0201_offset(uint32_t code, uint32_t *offset) {
    if(code >= 0x20 && code <= 0x7e) {
        if(code == 0x20)
            *offset = BFONT_BLANK;
        else if(code == 0x5c)
            *offset = BFONT_YEN;
        else if(code == 0x7e)
            *offset = BFONT_OVERBAR;
        else
            *offset = (code - 0x20) * BFONT_BYTES_PER_CHAR;

        return true;
    }

    if(code >= 0xa1 && code <= 0xdf) {
        *offset = BFONT_JISX_0201_160_255 +
                  (code - 0xa0) * BFONT_BYTES_PER_CHAR;
        return true;
    }

    return false;
}

static bool bfont_shift_jis_font_code(uint16_t code) {
    /* These are the assigned Shift-JIS spans backed by the three wide-font
       regions in the Boot ROM. Syntactically convertible codes also exist in
       the gaps, but their converted JIS slots do not contain assigned glyphs. */
    return (code >= 0x8140 && code <= 0x8396) ||
           (code >= 0x889f && code <= 0x9872) ||
           (code >= 0x989f && code <= 0xeaa4);
}

static void bfont_set_glyph(bfont_glyph_t *glyph, uint32_t offset,
                            uint16_t width, uint16_t height,
                            uint16_t data_size) {
    glyph->data = get_font_address() + offset;
    glyph->offset = offset;
    glyph->width = width;
    glyph->height = height;
    glyph->data_size = data_size;
}

bool bfont_lookup_glyph(bfont_glyph_set_t set, uint32_t code,
                        bfont_glyph_t *glyph) {
    uint32_t offset;
    uint16_t jis;

    if(!glyph)
        return false;

    memset(glyph, 0, sizeof(*glyph));

    switch(set) {
        case BFONT_GLYPH_ISO8859_1:
            if(code == 0x20)
                offset = BFONT_BLANK;
            else if(code >= 0x21 && code <= 0x7e)
                offset = (code - 0x20) * BFONT_BYTES_PER_CHAR;
            else if(code >= 0xa0 && code <= 0xff)
                offset = BFONT_ISO_8859_1_160_255 +
                         (code - 0xa0) * BFONT_BYTES_PER_CHAR;
            else
                return false;

            bfont_set_glyph(glyph, offset, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_CHAR);
            return true;

        case BFONT_GLYPH_JISX0201:
            if(!bfont_jisx0201_offset(code, &offset))
                return false;

            bfont_set_glyph(glyph, offset, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_CHAR);
            return true;

        case BFONT_GLYPH_JISX0208:
            if(code > UINT16_MAX ||
               !bfont_jisx0208_offset((uint16_t)code, &offset))
                return false;

            bfont_set_glyph(glyph, offset, BFONT_WIDE_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_WIDE_CHAR);
            return true;

        case BFONT_GLYPH_SHIFT_JIS:
            if(code <= 0xff) {
                if(!bfont_jisx0201_offset(code, &offset))
                    return false;

                bfont_set_glyph(glyph, offset, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                                BFONT_BYTES_PER_CHAR);
                return true;
            }

            if(code > UINT16_MAX ||
               !bfont_shift_jis_font_code((uint16_t)code) ||
               !bfont_sjis_to_jis((uint16_t)code, &jis) ||
               !bfont_jisx0208_offset(jis, &offset))
                return false;

            bfont_set_glyph(glyph, offset, BFONT_WIDE_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_WIDE_CHAR);
            return true;

        case BFONT_GLYPH_EUC_JP:
            if(code <= 0x7f) {
                if(!bfont_jisx0201_offset(code, &offset))
                    return false;

                bfont_set_glyph(glyph, offset, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                                BFONT_BYTES_PER_CHAR);
                return true;
            }

            if((code & 0xff00) == 0x8e00) {
                if(!bfont_jisx0201_offset(code & 0xff, &offset))
                    return false;

                bfont_set_glyph(glyph, offset, BFONT_THIN_WIDTH, BFONT_HEIGHT,
                                BFONT_BYTES_PER_CHAR);
                return true;
            }

            if(code > UINT16_MAX || (code & 0x8080) != 0x8080 ||
               !bfont_jisx0208_offset((uint16_t)euc2jis(code), &offset))
                return false;

            bfont_set_glyph(glyph, offset, BFONT_WIDE_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_WIDE_CHAR);
            return true;

        case BFONT_GLYPH_DREAMCAST_ICON:
            if(code >= BFONT_DREAMCAST_ICON_COUNT)
                return false;

            offset = BFONT_DC_ICON(code);
            bfont_set_glyph(glyph, offset, BFONT_WIDE_WIDTH, BFONT_HEIGHT,
                            BFONT_BYTES_PER_WIDE_CHAR);
            return true;

        case BFONT_GLYPH_VMU_ICON:
            if(code >= BFONT_VMU_ICON_COUNT)
                return false;

            offset = BFONT_VMU_DREAMCAST_SPECIFIC +
                     code * BFONT_ICON_DIMEN * BFONT_ICON_DIMEN / 8;
            bfont_set_glyph(glyph, offset, BFONT_ICON_DIMEN, BFONT_ICON_DIMEN,
                            BFONT_ICON_DIMEN * BFONT_ICON_DIMEN / 8);
            return true;

        default:
            return false;
    }
}

/* Given an ASCII character, find it in the BIOS font if possible */
uint8_t *bfont_find_char(uint32_t ch) {
    bfont_glyph_t glyph;

    if(bfont_lookup_glyph(BFONT_GLYPH_ISO8859_1, ch, &glyph))
        return glyph.data;

    /* Stock KOS used the blank JIS 0x2121 slot for space and replacement. */
    return get_font_address() + BFONT_BLANK;
}

/* JIS -> (kuten) -> address conversion */
uint8_t *bfont_find_char_jp(uint32_t ch) {
    bfont_glyph_t glyph;
    bfont_glyph_set_t set;

    switch(bfont_code_mode) {
        case BFONT_CODE_ISO8859_1:
            return NULL;
        case BFONT_CODE_EUC:
            set = BFONT_GLYPH_EUC_JP;
            break;
        case BFONT_CODE_SJIS:
            set = BFONT_GLYPH_SHIFT_JIS;
            break;
        default:
            assert_msg(0, "Unknown bfont encoding mode");
            return get_font_address() + BFONT_WIDE_START;
    }

    if(bfont_lookup_glyph(set, ch, &glyph) &&
       glyph.width == BFONT_WIDE_WIDTH)
        return glyph.data;

    return get_font_address() + BFONT_WIDE_START;
}


/* Half-width kana -> address conversion */
uint8_t *bfont_find_char_jp_half(uint32_t ch) {
    bfont_glyph_t glyph;

    if(bfont_lookup_glyph(BFONT_GLYPH_JISX0201, ch, &glyph))
        return glyph.data;

    return get_font_address() + BFONT_BLANK;
}

/* Draws one half-width row of a character to an output buffer of bit depth in bits per pixel */
static uint16_t *bfont_draw_one_row(uint16_t *b, uint16_t word, bool opaque, uint32_t fg, uint32_t bg, uint8_t bpp) {
    uint8_t x;
    uint32_t color = 0x0000;
    uint16_t write16 = 0x0000;
    uint16_t oldcolor = *b;

    if((bpp == 4)||(bpp == 8)) {
        /* For 4 or 8bpp we have to go 2 or 4 pixels at a time to properly write out in all cases. */
        uint8_t bMask = (bpp==4) ? 0xf : 0xff;
        uint8_t pix = 16/bpp;
        for(x = 0; x < BFONT_THIN_WIDTH; x++) {
            if(x%pix == 0) {
                oldcolor = *b;
                write16 = 0x0000;
            }

            if(word & (0x0800 >> x)) write16 |= fg<<(bpp*(x%pix));
            else {
                if(opaque)           write16 |= bg<<(bpp*(x%pix));
                else                 write16 |= oldcolor&(bMask<<(bpp*(x%pix)));
            }
            if(x%pix == (pix-1)) *b++ = write16;
        }
    }
    else {/* 16 or 32 */

        for(x = 0; x < BFONT_THIN_WIDTH; x++, b++) {
            if(word & (0x0800 >> x))
                color = fg;
            else {
                if(opaque)           color = bg;
                else                 continue;
            }
            if(bpp==16) *b = color & 0xffff;
            else if(bpp == 32) {*(uint32_t *)b = color; b++;}
        }
    }

    return b;
}

size_t bfont_draw_ex(void *buf, uint32_t bufwidth, uint32_t fg, uint32_t bg, 
                     uint8_t bpp, bool opaque, uint32_t c, bool wide, bool iskana) {
    uint8_t *ch;
    uint16_t word;
    uint8_t y;
    uint8_t *buffer = (uint8_t *)buf;

    /* If they're requesting a wide char and in the wrong format, kick this out */
    if(wide && (bfont_code_mode == BFONT_CODE_ISO8859_1)) {
        dbglog(DBG_ERROR, "bfont_draw_ex: can't draw wide in bfont mode %d\n", bfont_code_mode);
        return 0;
    }

    /* Just making sure we can draw the character we want to */
    if(bufwidth < (uint32_t)(BFONT_THIN_WIDTH*(wide+1))) {
        dbglog(DBG_ERROR, "bfont_draw_ex: buffer is too small to draw into\n");
        return 0;
    }

    if(lock_bfont() < 0) {
        dbglog(DBG_ERROR, "bfont_draw_ex: error requesting font access\n");
        return 0;
    }

    /* Translate the character */
    if(bfont_code_mode == BFONT_CODE_RAW)
        ch = get_font_address() + c;
    else if(wide && ((bfont_code_mode == BFONT_CODE_EUC) || (bfont_code_mode == BFONT_CODE_SJIS)))
        ch = bfont_find_char_jp(c);
    else {
        if(iskana)
            ch = bfont_find_char_jp_half(c);
        else
            ch = bfont_find_char(c);
    }

    /* Increment over the height of the font. 3bytes at a time (2 thin or 1 wide row) */
    for(y = 0; y < BFONT_HEIGHT; y+= (2-wide),ch+=((BFONT_THIN_WIDTH*2)/8)) {
        /* Do the first row, or half row */
        word = (((uint16_t)ch[0]) << 4) | ((ch[1] >> 4) & 0x0f);
        buffer = (uint8_t *)bfont_draw_one_row((uint16_t *)buffer, word, opaque, fg, bg, bpp);

        /* If we're thin, increment to next row, otherwise continue the row */
        if(!wide) buffer += ((bufwidth - BFONT_THIN_WIDTH)*bpp)/8;

        /* Do the second row, or second half */
        word = ((((uint16_t)ch[1]) << 8) & 0xf00) | ch[2];

        buffer = (uint8_t *)bfont_draw_one_row((uint16_t *)buffer, word, opaque, fg, bg, bpp);

        /* Increment to the next row. */
        if(!wide) buffer += ((bufwidth - BFONT_THIN_WIDTH)*bpp)/8;
        else buffer += ((bufwidth - BFONT_WIDE_WIDTH)*bpp)/8;
    }

    if(unlock_bfont() < 0)
        dbglog(DBG_ERROR, "bfont_draw_ex: error releasing font access\n");

    /* Return the horizontal distance covered in bytes */
    if(wide)
        return (BFONT_WIDE_WIDTH*bpp)/8;
    else
        return (BFONT_THIN_WIDTH*bpp)/8;
}

/* Draw half-width kana */
size_t bfont_draw_thin(void *b, uint32_t bufwidth, bool opaque, uint32_t c, bool iskana) {
    return bfont_draw_ex(b, bufwidth, bfont_fgcolor, bfont_bgcolor, 
                         bits_per_pixel(), opaque, c, false, iskana);
}

/* Compat function */
size_t bfont_draw(void *buffer, uint32_t bufwidth, bool opaque, uint32_t c) {
    return bfont_draw_ex(buffer, bufwidth, bfont_fgcolor, bfont_bgcolor, 
                        bits_per_pixel(), opaque, c, false, false);
}

/* Draw wide character */
size_t bfont_draw_wide(void *b, uint32_t bufwidth, bool opaque, uint32_t c) {
    return bfont_draw_ex(b, bufwidth, bfont_fgcolor, bfont_bgcolor, 
                         bits_per_pixel(), opaque, c, true, false);
}

void bfont_draw_str_ex(void *b, uint32_t width, uint32_t fg, uint32_t bg, 
                       uint8_t bpp, bool opaque, const char *str) {
    bool wideChr;
    uint16_t nChr, nMask;
    uint32_t line_start = 0;
    uint8_t *buffer = (uint8_t *)b;

    while(*str) {
        wideChr = false;
        nChr = *str & 0xff;

        if(nChr == '\n') {
            /* Move to the beginning of the next line */
            buffer = (uint8_t *)b + line_start + (width * BFONT_HEIGHT * (bpp / 8));
            line_start = buffer - (uint8_t *)b;
            str++;
            continue;
        }
        else if(nChr == '\t') {
            /* Draw four spaces on the current line */
            if(opaque) {
                nChr = bfont_code_mode == BFONT_CODE_ISO8859_1 ? 0x20 : 0xa0;
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, false);
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, false);
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, false);
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, false);
            }
            else /* Spaces are always thin width characters */
                buffer += (4 * ((BFONT_THIN_WIDTH * bpp)/8));
            
            str++;
            continue;
        }

        /* Non-western, non-ASCII character */
        if(bfont_code_mode != BFONT_CODE_ISO8859_1 && (nChr & 0x80)) {
            switch(bfont_code_mode) {
                case BFONT_CODE_EUC:

                    /* Check if the character is the 'SS2' character in EUC-JP */
                    if(nChr == 0x8e) {
                        str++;
                        nChr = *str & 0xff;

                        /* Is a valid half-width katakana character? */
                        if((nChr < 0xa1) || (nChr > 0xdf))
                            nChr = 0xa0;    /* Blank Space */
                    }
                    else
                        wideChr = true;

                    break;
                case BFONT_CODE_SJIS:
                    nMask = nChr & 0xf0;

                    /* Check if the character is part of the valid Shift ranges */
                    if((nMask == 0x80) || (nMask == 0x90) || (nMask == 0xe0))
                        wideChr = true;

                    break;
                default:
                    assert_msg(0, "Unknown bfont encoding mode");
            }

            if(wideChr) {
                str++;
                nChr = (nChr << 8) | (*str & 0xff);
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, true, false);
            }
            else
                buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, true);
        }
        else
            buffer += bfont_draw_ex(buffer, width, fg, bg, bpp, opaque, nChr, false, false);

        str++;
    }
}

void bfont_draw_str_ex_vfmt(void *b, uint32_t width, uint32_t fg, uint32_t bg,
                            uint8_t bpp, bool opaque, const char *fmt,
                            va_list *var_args) {
    /* Maximum of 1060 thin characters onscreen, plus padding for multiple of 32. */
    char string[1088];

    vsnprintf(string, sizeof(string), fmt, *var_args);
    bfont_draw_str_ex(b, width, fg, bg, bpp, opaque, string);
}

/* Draw string of full-width (wide) and half-width (thin) characters
   Note that this handles the case of mixed encodings unless Japanese
   support is disabled (BFONT_CODE_ISO8859_1).
*/
void bfont_draw_str_ex_fmt(void *b, uint32_t width, uint32_t fg, uint32_t bg, uint8_t bpp,
                           bool opaque, const char *fmt, ...) {
    va_list var_args;
    va_start(var_args, fmt);

    bfont_draw_str_ex_vfmt(b, width, fg, bg, bpp, opaque, fmt, &var_args);

    va_end(var_args);
}

void bfont_draw_str(void *b, uint32_t width, bool opaque, const char *str) {
    bfont_draw_str_ex(b, width, bfont_fgcolor, bfont_bgcolor,
                     bits_per_pixel(), opaque, str);
}

void bfont_draw_str_fmt(void *b, uint32_t width, bool opaque, const char *fmt,
                        ...) {
    va_list var_args;
    va_start(var_args, fmt);

    bfont_draw_str_ex_vfmt(b, width, bfont_fgcolor, bfont_bgcolor,
                           bits_per_pixel(), opaque, fmt, &var_args);

    va_end(var_args);
}

void bfont_draw_str_vram_vfmt(uint32_t x, uint32_t y, uint32_t fg, 
                              uint32_t bg, bool opaque, const char *fmt, 
                              va_list *var_args) {
    uint32_t bpp = bits_per_pixel();
    void *vram = vram_s;
    uint32_t offset = (y * vid_mode->width + x);

    if(bpp == 16)
        vram = (uint16_t *)vram + offset;
    else if(bpp == 32)
        vram = (uint32_t *)vram + offset;

    bfont_draw_str_ex_vfmt(vram, vid_mode->width, fg, bg, bpp, opaque, fmt,
                           var_args);
}

void bfont_draw_str_vram_fmt(uint32_t x, uint32_t y, bool opaque, 
                            const char *fmt, ...) {
    va_list var_args;
    va_start(var_args, fmt);
    
    bfont_draw_str_vram_vfmt(x, y, bfont_fgcolor, bfont_bgcolor, opaque, fmt, 
                            &var_args);

    va_end(var_args);
}

uint8_t *bfont_find_icon(bfont_vmu_icon_t icon) {
    bfont_glyph_t glyph;

    if(!bfont_lookup_glyph(BFONT_GLYPH_VMU_ICON, icon, &glyph))
        return NULL;

    return glyph.data;
}
