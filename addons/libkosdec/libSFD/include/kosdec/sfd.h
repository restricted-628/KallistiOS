/* Original bounded MPEG-1 PS/Sofdec fixture reader. Video only. */
#ifndef KOSDEC_SFD_H
#define KOSDEC_SFD_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_SFD_MAX_BYTES (4u * 1024u * 1024u)
enum kosdec_sfd_result { KOSDEC_SFD_OK, KOSDEC_SFD_INVALID, KOSDEC_SFD_TRUNCATED, KOSDEC_SFD_LIMIT, KOSDEC_SFD_UNSUPPORTED };
struct kosdec_sfd_info {
    size_t packs, video_packets, audio_packets, video_bytes, timestamps;
    uint64_t first_pts, last_pts;
};
/* NULL output performs validated sizing only. Otherwise capacity is checked
   before every write. Input/output must not overlap. No retained pointers. */
enum kosdec_sfd_result kosdec_sfd_video_extract(const uint8_t *input, size_t bytes,
                                  uint8_t *output, size_t capacity,
                                  struct kosdec_sfd_info *info);
#ifdef __cplusplus
}
#endif
#endif
