/* KallistiOS ##version##

   dc/pvr/pvr_chunk_animation_catalog.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_animation_catalog.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_CLIP_COUNT_OFFSET = 12,
    HEADER_RECORD_BYTES_OFFSET = 16,
    HEADER_RESERVED0_OFFSET = 18,
    HEADER_RECORDS_BYTES_OFFSET = 20,
    HEADER_STRINGS_BYTES_OFFSET = 24,
    HEADER_PAYLOAD_CRC_OFFSET = 28,
    HEADER_RESERVED_OFFSET = 32,
    HEADER_CRC_OFFSET = 60,
    HEADER_CRC_BYTES = 60,
    RECORD_TRANSFORM_OFFSET = 0,
    RECORD_MORPH_OFFSET = 4,
    RECORD_NAME_OFFSET = 8,
    RECORD_NAME_BYTES_OFFSET = 12,
    RECORD_START_TIME_OFFSET = 16,
    RECORD_END_TIME_OFFSET = 20,
    RECORD_FLAGS_OFFSET = 24,
    RECORD_RESERVED_OFFSET = 28
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_float(const uint8_t *bytes) {
    uint32_t word = read_le32(bytes);
    float value;

    memcpy(&value, &word, sizeof(value));
    return value;
}

static uint32_t crc32_bytes(const void *data, size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = UINT32_MAX;
    size_t index;

    for(index = 0; index < size; ++index) {
        unsigned bit;

        crc ^= bytes[index];
        for(bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^
                  (UINT32_C(0xedb88320) &
                   (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static void decode_clip(
    const pvr_chunk_animation_catalog_view_t *view, size_t index,
    pvr_chunk_animation_catalog_clip_t *clip) {
    const uint8_t *record = (const uint8_t *)view->records +
        index * PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES;
    uint32_t name_offset = read_le32(record + RECORD_NAME_OFFSET);

    clip->transform_ordinal = read_le32(
        record + RECORD_TRANSFORM_OFFSET);
    clip->morph_ordinal = read_le32(record + RECORD_MORPH_OFFSET);
    clip->name = view->strings + name_offset;
    clip->name_bytes = read_le32(record + RECORD_NAME_BYTES_OFFSET);
    clip->start_time = read_float(record + RECORD_START_TIME_OFFSET);
    clip->end_time = read_float(record + RECORD_END_TIME_OFFSET);
}

int pvr_chunk_animation_catalog_open(
    const void *data, size_t size,
    pvr_chunk_animation_catalog_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_animation_catalog_view_t parsed;
    uint32_t file_bytes;
    uint32_t clip_count;
    uint32_t records_bytes;
    uint32_t string_bytes;
    size_t expected_name_offset = 0;
    size_t index;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_ANIMATION_CATALOG_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_ANIMATION_CATALOG_VERSION ||
       read_le16(bytes + 6) !=
           PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES ||
       read_le16(bytes + HEADER_RECORD_BYTES_OFFSET) !=
           PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES ||
       read_le16(bytes + HEADER_RESERVED0_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    for(index = HEADER_RESERVED_OFFSET; index < HEADER_CRC_OFFSET; ++index) {
        if(bytes[index]) {
            errno = EILSEQ;
            return -1;
        }
    }
    file_bytes = read_le32(bytes + 8);
    clip_count = read_le32(bytes + HEADER_CLIP_COUNT_OFFSET);
    records_bytes = read_le32(bytes + HEADER_RECORDS_BYTES_OFFSET);
    string_bytes = read_le32(bytes + HEADER_STRINGS_BYTES_OFFSET);
    if(file_bytes != size || !clip_count ||
       clip_count > UINT32_MAX /
           PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES ||
       records_bytes != clip_count *
           PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES ||
       records_bytes > size - PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES ||
       string_bytes != size - PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES -
                           records_bytes ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES,
           size - PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = size;
    parsed.records = bytes + PVR_CHUNK_ANIMATION_CATALOG_HEADER_BYTES;
    parsed.strings = (const char *)parsed.records + records_bytes;
    parsed.clip_count = clip_count;
    parsed.string_bytes = string_bytes;
    for(index = 0; index < parsed.clip_count; ++index) {
        const uint8_t *record = (const uint8_t *)parsed.records +
            index * PVR_CHUNK_ANIMATION_CATALOG_RECORD_BYTES;
        uint32_t transform = read_le32(record + RECORD_TRANSFORM_OFFSET);
        uint32_t morph = read_le32(record + RECORD_MORPH_OFFSET);
        uint32_t name_offset = read_le32(record + RECORD_NAME_OFFSET);
        uint32_t name_bytes = read_le32(record + RECORD_NAME_BYTES_OFFSET);
        float start_time = read_float(record + RECORD_START_TIME_OFFSET);
        float end_time = read_float(record + RECORD_END_TIME_OFFSET);
        size_t previous;

        if((transform == PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
            morph == PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE) ||
           name_offset != expected_name_offset ||
           name_bytes > parsed.string_bytes - expected_name_offset ||
           !isfinite(start_time) || !isfinite(end_time) ||
           start_time >= end_time ||
           read_le32(record + RECORD_FLAGS_OFFSET) ||
           read_le32(record + RECORD_RESERVED_OFFSET) ||
           (name_bytes && memchr(parsed.strings + name_offset, '\0',
                                 name_bytes))) {
            errno = EILSEQ;
            return -1;
        }
        for(previous = 0; name_bytes && previous < index; ++previous) {
            pvr_chunk_animation_catalog_clip_t candidate;

            decode_clip(&parsed, previous, &candidate);
            if(candidate.name_bytes == name_bytes &&
               !memcmp(candidate.name, parsed.strings + name_offset,
                       name_bytes)) {
                errno = EILSEQ;
                return -1;
            }
        }
        expected_name_offset += name_bytes;
    }
    if(expected_name_offset != parsed.string_bytes) {
        errno = EILSEQ;
        return -1;
    }
    *view = parsed;
    return 0;
}

int pvr_chunk_animation_catalog_clip_get(
    const pvr_chunk_animation_catalog_view_t *view, size_t index,
    pvr_chunk_animation_catalog_clip_t *clip) {
    pvr_chunk_animation_catalog_view_t checked;

    if(clip)
        memset(clip, 0, sizeof(*clip));
    if(!view || !clip || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_catalog_open(
           view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.clip_count) {
        errno = ENOENT;
        return -1;
    }
    decode_clip(&checked, index, clip);
    return 0;
}

int pvr_chunk_animation_catalog_find(
    const pvr_chunk_animation_catalog_view_t *view,
    const char *name, size_t name_bytes, size_t *index,
    pvr_chunk_animation_catalog_clip_t *clip) {
    pvr_chunk_animation_catalog_view_t checked;
    size_t candidate;

    if(index)
        *index = 0;
    if(clip)
        memset(clip, 0, sizeof(*clip));
    if(!view || !name || !name_bytes || !clip || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_catalog_open(
           view->data, view->size, &checked) < 0)
        return -1;
    for(candidate = 0; candidate < checked.clip_count; ++candidate) {
        pvr_chunk_animation_catalog_clip_t decoded;

        decode_clip(&checked, candidate, &decoded);
        if(decoded.name_bytes == name_bytes &&
           !memcmp(decoded.name, name, name_bytes)) {
            if(index)
                *index = candidate;
            *clip = decoded;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

int pvr_chunk_animation_catalog_validate_asset(
    const pvr_chunk_animation_catalog_view_t *view,
    const pvr_chunk_asset_view_t *asset) {
    pvr_chunk_animation_catalog_view_t checked;
    pvr_chunk_asset_view_t checked_asset;
    size_t index;

    if(!view || !asset || !view->data || !asset->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_animation_catalog_open(
           view->data, view->size, &checked) < 0 ||
       pvr_chunk_asset_open(asset->data, asset->size, &checked_asset) < 0)
        return -1;
    for(index = 0; index < checked.clip_count; ++index) {
        pvr_chunk_animation_catalog_clip_t clip;
        pvr_chunk_asset_section_t section;

        decode_clip(&checked, index, &clip);
        if((clip.transform_ordinal !=
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
            pvr_chunk_asset_section_find(
                &checked_asset, PVR_CHUNK_ASSET_SECTION_ANIMATION,
                clip.transform_ordinal, &section) < 0) ||
           (clip.morph_ordinal !=
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE &&
            pvr_chunk_asset_section_find(
                &checked_asset,
                PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
                clip.morph_ordinal, &section) < 0)) {
            if(errno == ENOENT)
                errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}
