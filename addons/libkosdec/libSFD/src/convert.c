#include <kosdec/sfd_video.h>
#include <stdio.h>

#include <sh4zam/shz_matrix.h>
#include <sh4zam/shz_scalar.h>

/* Same declared BT.601 approximation, represented as binary32 dyadic values.
   Columns map centered Y,Cb,Cr,1 into R,G,B,0. */
static const shz_mat4x4_t color_matrix = {.elem = {
    298.0f / 256.0f, 298.0f / 256.0f, 298.0f / 256.0f, 0.0f,
    0.0f, -100.0f / 256.0f, 516.0f / 256.0f, 0.0f,
    409.0f / 256.0f, -208.0f / 256.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f
}};
static unsigned channel(float value) {
    /* Byte inputs and fixed matrix guarantee finite, bounded values. Keep
       strict-mode builtin min/max from introducing libm calls per pixel. */
    value = shz_floorf(value + 0.5f);
    if(value < 0.0f) value = 0.0f;
    if(value > 255.0f) value = 255.0f;
    return (unsigned)value;
}
/* Hand-authored rows for the small diagnostic alphabet; no BIOS font needed. */
static const struct { char c; uint8_t row[7]; } glyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'A',{14,17,17,31,17,17,17}}, {'C',{14,17,16,16,16,17,14}},
    {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}},
    {'F',{31,16,16,30,16,16,16}}, {'G',{14,17,16,23,17,17,14}},
    {'H',{17,17,17,31,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'W',{17,17,17,21,21,21,10}}, {'/',{1,2,2,4,8,8,16}},
    {'.',{0,0,0,0,0,6,6}}, {'-',{0,0,0,31,0,0,0}}
};
static void text_line(uint16_t *pixels, unsigned y, const char *text) {
    for(unsigned i = 0; text[i] && i < 52; ++i) {
        const uint8_t *rows = NULL;
        for(unsigned g = 0; g < sizeof(glyphs) / sizeof(glyphs[0]); ++g)
            if(glyphs[g].c == text[i]) { rows = glyphs[g].row; break; }
        if(!rows) continue;
        for(unsigned r = 0; r < 7; ++r) for(unsigned x = 0; x < 5; ++x)
            if(rows[r] & (16u >> x)) pixels[(y + r) * 320 + 2 + i * 6 + x] = 0xffff;
    }
}
void kosdec_sfd_overlay(uint16_t *pixels, unsigned decoded,
                 unsigned show_fps10, unsigned decode_fps10) {
    char line[64], show[16], decode[16];
    for(unsigned i = 0; i < 18 * 320; ++i) pixels[i] = 0;
    if(show_fps10) snprintf(show, sizeof(show), "%u.%u", show_fps10 / 10, show_fps10 % 10);
    else snprintf(show, sizeof(show), "--.-");
    if(decode_fps10) snprintf(decode, sizeof(decode), "%u.%u", decode_fps10 / 10, decode_fps10 % 10);
    else snprintf(decode, sizeof(decode), "--.-");
    snprintf(line, sizeof(line), "DECODED %u/360 SHOW FPS %s", decoded, show);
    text_line(pixels, 1, line);
    snprintf(line, sizeof(line), "DECODE FPS %s TARGET 30", decode);
    text_line(pixels, 10, line);
}
void kosdec_sfd_rgb565(const uint8_t *planes, uint16_t *pixels) {
    const uint8_t *cb = planes + 320 * 240, *cr = cb + 160 * 120;
    shz_mat4x4_t saved;
    shz_xmtrx_store_4x4(&saved);
    shz_xmtrx_load_4x4(&color_matrix);
    for(unsigned y = 0; y < 240; ++y) {
        for(unsigned x = 0; x < 320; ++x) {
            unsigned c = (y / 2) * 160 + x / 2;
            shz_vec4_t rgb = shz_xmtrx_transform_vec4(shz_vec4_init(
                (float)planes[y * 320 + x] - 16.0f,
                (float)cb[c] - 128.0f, (float)cr[c] - 128.0f, 1.0f));
            unsigned r = channel(rgb.x), g = channel(rgb.y), b = channel(rgb.z);
            pixels[y * 320 + x] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        }
    }
    shz_xmtrx_load_4x4(&saved);
}
