/* Source-defined packet walker. No proprietary implementation or tables. */
#include <kosdec/sfd.h>
#include <string.h>

static int timestamp(const uint8_t *p, unsigned prefix, uint64_t *value) {
    if((p[0] >> 4) != prefix || !(p[0] & 1) || !(p[2] & 1) || !(p[4] & 1))
        return -1;
    *value = ((uint64_t)(p[0] & 14) << 29) | ((uint64_t)p[1] << 22)
           | ((uint64_t)(p[2] & 254) << 14) | ((uint64_t)p[3] << 7) | (p[4] >> 1);
    return 0;
}

enum kosdec_sfd_result kosdec_sfd_video_extract(const uint8_t *input, size_t bytes,
                                  uint8_t *output, size_t capacity,
                                  struct kosdec_sfd_info *info) {
    if(!info) return KOSDEC_SFD_INVALID;
    memset(info, 0, sizeof(*info));
    if(!input) return KOSDEC_SFD_INVALID;
    if(bytes > KOSDEC_SFD_MAX_BYTES) return KOSDEC_SFD_LIMIT;
    if(bytes < 12) return KOSDEC_SFD_TRUNCATED;
    if(memcmp(input, "\0\0\1\xba", 4)) return KOSDEC_SFD_INVALID;
    size_t pos = 0;
    int metadata = 0, system = 0;
    while(pos < bytes) {
        if(bytes - pos < 4) return KOSDEC_SFD_TRUNCATED;
        if(memcmp(input + pos, "\0\0\1", 3)) return KOSDEC_SFD_INVALID;
        unsigned code = input[pos + 3];
        if(code == 0xb9) {
            size_t tail = bytes - pos - 4;
            size_t padding = (2048 - ((pos + 4) % 2048)) % 2048;
            if(tail < padding) return KOSDEC_SFD_TRUNCATED;
            if(tail != padding) return KOSDEC_SFD_INVALID;
            for(size_t i = pos + 4; i < bytes; ++i)
                if(input[i] != 255) return KOSDEC_SFD_INVALID;
            return metadata && system && info->video_bytes ? KOSDEC_SFD_OK : KOSDEC_SFD_INVALID;
        }
        if(code == 0xba) {
            if(bytes - pos < 12) return KOSDEC_SFD_TRUNCATED;
            const uint8_t *h = input + pos + 4;
            uint64_t scr;
            if(timestamp(h, 2, &scr) || !(h[5] & 128) || !(h[7] & 1))
                return KOSDEC_SFD_UNSUPPORTED;
            ++info->packs; pos += 12; continue;
        }
        if(bytes - pos < 6) return KOSDEC_SFD_TRUNCATED;
        size_t length = ((size_t)input[pos + 4] << 8) | input[pos + 5];
        pos += 6;
        if(length > bytes - pos) return KOSDEC_SFD_TRUNCATED;
        size_t end = pos + length;
        if(code == 0xbb) {
            if(length < 6) return KOSDEC_SFD_INVALID;
            system = 1;
        }
        else if(code == 0xbf) {
            /* Bounded profile identification, not a general metadata parser. */
            static const char marker[] = "SofdecStream";
            for(size_t i = pos; i + sizeof(marker) - 1 <= end; ++i)
                if(!memcmp(input + i, marker, sizeof(marker) - 1)) metadata = 1;
        }
        else if(code == 0xc0) ++info->audio_packets; /* Audio deliberately skipped. */
        else if(code == 0xe0) {
            size_t p = pos;
            while(p < end && input[p] == 255) ++p;
            if(p == end) return KOSDEC_SFD_INVALID;
            if((input[p] & 0xc0) == 0x40) {
                if(end - p < 2) return KOSDEC_SFD_TRUNCATED;
                p += 2;
            }
            if(p == end) return KOSDEC_SFD_TRUNCATED;
            unsigned prefix = input[p] >> 4;
            if(prefix == 2 || prefix == 3) {
                size_t size = prefix == 3 ? 10 : 5;
                uint64_t pts, dts;
                if(end - p < size) return KOSDEC_SFD_TRUNCATED;
                if(timestamp(input + p, prefix, &pts) ||
                   (prefix == 3 && timestamp(input + p + 5, 1, &dts))) return KOSDEC_SFD_INVALID;
                if(!info->timestamps) info->first_pts = pts;
                info->last_pts = pts; ++info->timestamps;
                p += size;
            }
            else if(input[p] == 15) ++p;
            else return KOSDEC_SFD_UNSUPPORTED;
            size_t payload = end - p;
            /* The fixture includes a valid header-only E0 packet near EOF. */
            if(payload > KOSDEC_SFD_MAX_BYTES - info->video_bytes) return KOSDEC_SFD_LIMIT;
            if(output) {
                if(info->video_bytes > capacity || payload > capacity - info->video_bytes)
                    return KOSDEC_SFD_LIMIT;
                memcpy(output + info->video_bytes, input + p, payload);
            }
            info->video_bytes += payload;
            ++info->video_packets;
        }
        else if(code != 0xbe) return KOSDEC_SFD_UNSUPPORTED;
        pos = end;
    }
    return KOSDEC_SFD_TRUNCATED; /* Program end is required. */
}
