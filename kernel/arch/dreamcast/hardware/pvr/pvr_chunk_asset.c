/* KallistiOS ##version##

   dc/pvr/pvr_chunk_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_asset.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_CRC_BYTES = 80,
    HEADER_CRC_OFFSET = 80,
    HEADER_RESERVED_OFFSET = 84,
    SECTION_VERTEX_OFFSET = 32,
    SECTION_POLYGON_OFFSET = 56,
    SECTION_DESCRIPTOR_BYTES = 24
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static float read_le_float(const uint8_t *bytes) {
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
                  (UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int add_size(size_t left, size_t right, size_t *result) {
    if(left > SIZE_MAX - right) {
        errno = EILSEQ;
        return -1;
    }
    *result = left + right;
    return 0;
}

static int align_size(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1u;

    if(value > SIZE_MAX - mask) {
        errno = EOVERFLOW;
        return -1;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

static int parse_section(const uint8_t *data, size_t file_bytes,
                         size_t descriptor_offset, size_t natural_alignment,
                         pvr_chunk_asset_section_t *section,
                         size_t *section_offset, size_t *section_end) {
    const uint8_t *descriptor = data + descriptor_offset;
    uint32_t offset = read_le32(descriptor);
    uint32_t stored = read_le32(descriptor + 4);
    uint32_t decoded = read_le32(descriptor + 8);
    uint16_t codec = read_le16(descriptor + 16);
    uint16_t flags = read_le16(descriptor + 18);
    uint32_t dictionary_id = read_le32(descriptor + 20);
    size_t end;

    if(!stored || !decoded || flags ||
       offset < PVR_CHUNK_ASSET_HEADER_BYTES ||
       (offset & (PVR_CHUNK_ASSET_ALIGNMENT - 1u)) ||
       decoded % natural_alignment || add_size(offset, stored, &end) < 0 ||
       end > file_bytes) {
        errno = EILSEQ;
        return -1;
    }
    if(codec != PVR_CHUNK_ASSET_CODEC_RAW &&
       codec != PVR_CHUNK_ASSET_CODEC_LZ4_FRAME) {
        errno = ENOTSUP;
        return -1;
    }
    if(codec == PVR_CHUNK_ASSET_CODEC_RAW &&
       (stored != decoded || dictionary_id)) {
        errno = EILSEQ;
        return -1;
    }

    section->stored_data = data + offset;
    section->stored_bytes = stored;
    section->decoded_bytes = decoded;
    section->decoded_crc32 = read_le32(descriptor + 12);
    section->dictionary_id = dictionary_id;
    section->codec = (pvr_chunk_asset_codec_t)codec;
    *section_offset = offset;
    *section_end = end;
    return 0;
}

int pvr_chunk_asset_open(const void *data, size_t size,
                         pvr_chunk_asset_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_asset_view_t parsed;
    uint32_t file_bytes;
    size_t vertex_offset;
    size_t vertex_end;
    size_t polygon_offset;
    size_t polygon_end;
    size_t reserved;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_ASSET_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_ASSET_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_ASSET_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_ASSET_HEADER_BYTES ||
       read_le32(bytes + 12) != 0) {
        errno = EILSEQ;
        return -1;
    }
    file_bytes = read_le32(bytes + 8);
    if(file_bytes < PVR_CHUNK_ASSET_HEADER_BYTES || file_bytes > size ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    for(reserved = HEADER_RESERVED_OFFSET;
        reserved < PVR_CHUNK_ASSET_HEADER_BYTES; ++reserved) {
        if(bytes[reserved]) {
            errno = EILSEQ;
            return -1;
        }
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.center[0] = read_le_float(bytes + 16);
    parsed.center[1] = read_le_float(bytes + 20);
    parsed.center[2] = read_le_float(bytes + 24);
    parsed.radius = read_le_float(bytes + 28);
    if(!isfinite(parsed.center[0]) || !isfinite(parsed.center[1]) ||
       !isfinite(parsed.center[2]) || !isfinite(parsed.radius) ||
       parsed.radius < 0.0f ||
       parse_section(bytes, file_bytes, SECTION_VERTEX_OFFSET,
                     sizeof(uint32_t), &parsed.vertex, &vertex_offset,
                     &vertex_end) < 0 ||
       parse_section(bytes, file_bytes, SECTION_POLYGON_OFFSET,
                     sizeof(uint16_t), &parsed.polygon, &polygon_offset,
                     &polygon_end) < 0)
        return -1;

    if(!(vertex_end <= polygon_offset || polygon_end <= vertex_offset)) {
        errno = EILSEQ;
        return -1;
    }

    *view = parsed;
    return 0;
}

static int section_needs_copy(const pvr_chunk_asset_section_t *section,
                              size_t natural_alignment) {
    return section->codec != PVR_CHUNK_ASSET_CODEC_RAW ||
           ((uintptr_t)section->stored_data & (natural_alignment - 1u));
}

int pvr_chunk_asset_workspace_query(
    const pvr_chunk_asset_view_t *view,
    pvr_chunk_asset_workspace_requirements_t *requirements) {
    pvr_chunk_asset_view_t checked;
    pvr_chunk_asset_workspace_requirements_t result;
    size_t cursor = 0;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!view || !requirements || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.alignment = PVR_CHUNK_ASSET_ALIGNMENT;
    result.copies_vertex = section_needs_copy(&checked.vertex,
                                              sizeof(uint32_t));
    result.copies_polygon = section_needs_copy(&checked.polygon,
                                               sizeof(uint16_t));
    if(result.copies_vertex) {
        if(align_size(cursor, result.alignment, &result.vertex_offset) < 0 ||
           add_size(result.vertex_offset, checked.vertex.decoded_bytes,
                    &cursor) < 0)
            return -1;
    }
    if(result.copies_polygon) {
        if(align_size(cursor, result.alignment, &result.polygon_offset) < 0 ||
           add_size(result.polygon_offset, checked.polygon.decoded_bytes,
                    &cursor) < 0)
            return -1;
    }
    result.bytes = cursor;
    *requirements = result;
    return 0;
}

static int load_section(const pvr_chunk_asset_section_t *section,
                        int copies, size_t offset,
                        pvr_chunk_asset_decoder_t decoder,
                        void *decoder_data, uint8_t *workspace,
                        const void **decoded) {
    void *destination;

    if(!copies) {
        *decoded = section->stored_data;
    }
    else {
        destination = workspace + offset;
        if(section->codec == PVR_CHUNK_ASSET_CODEC_RAW)
            memcpy(destination, section->stored_data, section->decoded_bytes);
        else if(!decoder) {
            errno = ENOTSUP;
            return -1;
        }
        else if(decoder(section, destination, section->decoded_bytes,
                        decoder_data) < 0)
            return -1;
        *decoded = destination;
    }

    if(crc32_bytes(*decoded, section->decoded_bytes) !=
       section->decoded_crc32) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_asset_load(const pvr_chunk_asset_view_t *view,
                         pvr_chunk_asset_decoder_t decoder,
                         void *decoder_data, void *workspace,
                         size_t workspace_bytes,
                         pvr_chunk_model_view_t *model_view) {
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_asset_view_t checked;
    pvr_chunk_model_t model;
    const void *vertex;
    const void *polygon;

    if(model_view)
        memset(model_view, 0, sizeof(*model_view));
    if(!view || !model_view || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0 ||
       pvr_chunk_asset_workspace_query(&checked, &requirements) < 0)
        return -1;
    if(requirements.bytes &&
       (!workspace || workspace_bytes < requirements.bytes ||
        ((uintptr_t)workspace & (requirements.alignment - 1u)))) {
        errno = workspace && workspace_bytes < requirements.bytes ? ENOSPC :
                                                                    EINVAL;
        return -1;
    }

    if(load_section(&checked.vertex, requirements.copies_vertex,
                    requirements.vertex_offset, decoder, decoder_data,
                    workspace, &vertex) < 0 ||
       load_section(&checked.polygon, requirements.copies_polygon,
                    requirements.polygon_offset, decoder, decoder_data,
                    workspace, &polygon) < 0)
        return -1;

    model.vertex_words = vertex;
    model.vertex_word_count = checked.vertex.decoded_bytes / sizeof(uint32_t);
    model.polygon_words = polygon;
    model.polygon_word_count = checked.polygon.decoded_bytes / sizeof(uint16_t);
    memcpy(model.center, checked.center, sizeof(model.center));
    model.radius = checked.radius;
    return pvr_chunk_model_open(&model, model_view);
}
