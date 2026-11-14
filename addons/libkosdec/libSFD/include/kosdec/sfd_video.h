#ifndef KOSDEC_SFD_VIDEO_CONVERT_H
#define KOSDEC_SFD_VIDEO_CONVERT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Fixed 320x240 planar Y, Cb, Cr 4:2:0 -> 320x240 RGB565.
   Source 115200 bytes, destination 76800 words; nonoverlapping. */
void kosdec_sfd_rgb565(const uint8_t *planes, uint16_t *pixels);
/* Original 5x7 diagnostic font. Overwrites only the top 18 RGB565 rows.
   FPS values in tenths; zero means no completed measurement interval yet. */
void kosdec_sfd_overlay(uint16_t *pixels, unsigned decoded,
                 unsigned show_fps10, unsigned decode_fps10);
#ifdef __cplusplus
}
#endif
#endif
