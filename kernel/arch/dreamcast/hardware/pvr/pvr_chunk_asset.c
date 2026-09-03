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
    SECTION_DESCRIPTOR_BYTES = 24,
    DIRECTORY_SECTION_COUNT_OFFSET = 32,
    DIRECTORY_OFFSET_OFFSET = 36,
    DIRECTORY_BYTES_OFFSET = 40,
    DIRECTORY_CRC_OFFSET = 44,
    DIRECTORY_RESERVED_OFFSET = 48,
    DIRECTORY_HEADER_CRC_OFFSET = 60,
    DIRECTORY_HEADER_CRC_BYTES = 60
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

static int ranges_overlap(const void *left, size_t left_bytes,
                          const void *right, size_t right_bytes) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if(!left_bytes || !right_bytes)
        return 0;
    if(left_start > UINTPTR_MAX - left_bytes ||
       right_start > UINTPTR_MAX - right_bytes)
        return 1;
    return left_start < right_start + right_bytes &&
           right_start < left_start + left_bytes;
}

static int parse_pcm1_section(
    const uint8_t *data, size_t file_bytes, size_t descriptor_offset,
    size_t natural_alignment, uint32_t type,
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
    section->type = type;
    section->flags = 0;
    section->alignment = natural_alignment;
    *section_offset = offset;
    *section_end = end;
    return 0;
}

static int open_pcm1(const void *data, size_t size,
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
    parsed.version = PVR_CHUNK_ASSET_VERSION;
    parsed.header_bytes = PVR_CHUNK_ASSET_HEADER_BYTES;
    parsed.section_count = 2;
    parsed.model_count = 1;
    parsed.center[0] = read_le_float(bytes + 16);
    parsed.center[1] = read_le_float(bytes + 20);
    parsed.center[2] = read_le_float(bytes + 24);
    parsed.radius = read_le_float(bytes + 28);
    if(!isfinite(parsed.center[0]) || !isfinite(parsed.center[1]) ||
       !isfinite(parsed.center[2]) || !isfinite(parsed.radius) ||
       parsed.radius < 0.0f ||
       parse_pcm1_section(
           bytes, file_bytes, SECTION_VERTEX_OFFSET, sizeof(uint32_t),
           PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, &parsed.vertex,
           &vertex_offset, &vertex_end) < 0 ||
       parse_pcm1_section(
           bytes, file_bytes, SECTION_POLYGON_OFFSET, sizeof(uint16_t),
           PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, &parsed.polygon,
           &polygon_offset, &polygon_end) < 0)
        return -1;

    if(!(vertex_end <= polygon_offset || polygon_end <= vertex_offset)) {
        errno = EILSEQ;
        return -1;
    }

    *view = parsed;
    return 0;
}

static int power_of_two(size_t value) {
    return value && !(value & (value - 1u));
}

static int parse_pcm2_section(
    const uint8_t *data, size_t file_bytes, const uint8_t *descriptor,
    size_t minimum_offset, pvr_chunk_asset_section_t *section,
    size_t *section_offset, size_t *section_end) {
    uint32_t type = read_le32(descriptor);
    uint32_t flags = read_le32(descriptor + 4);
    uint32_t offset = read_le32(descriptor + 8);
    uint32_t stored = read_le32(descriptor + 12);
    uint32_t decoded = read_le32(descriptor + 16);
    uint32_t dictionary_id = read_le32(descriptor + 24);
    uint16_t codec = read_le16(descriptor + 28);
    uint16_t alignment = read_le16(descriptor + 30);
    size_t end;

    if(!type || flags || !stored || !decoded ||
       !power_of_two(alignment) || alignment > PVR_CHUNK_ASSET_ALIGNMENT ||
       offset < minimum_offset ||
       (offset & (PVR_CHUNK_ASSET_ALIGNMENT - 1u)) ||
       add_size(offset, stored, &end) < 0 || end > file_bytes) {
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

    memset(section, 0, sizeof(*section));
    section->stored_data = data + offset;
    section->stored_bytes = stored;
    section->decoded_bytes = decoded;
    section->decoded_crc32 = read_le32(descriptor + 20);
    section->dictionary_id = dictionary_id;
    section->codec = (pvr_chunk_asset_codec_t)codec;
    section->type = type;
    section->flags = flags;
    section->alignment = alignment;
    *section_offset = offset;
    *section_end = end;
    return 0;
}

static int open_pcm2(const void *data, size_t size,
                     pvr_chunk_asset_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_asset_view_t parsed;
    pvr_chunk_asset_section_t section;
    uint32_t file_bytes;
    uint32_t section_count;
    uint32_t directory_offset;
    uint32_t directory_bytes;
    size_t directory_end;
    size_t previous_end;
    size_t index;
    size_t vertex_count = 0;
    size_t polygon_count = 0;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_ASSET_DIRECTORY_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_ASSET_DIRECTORY_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES ||
       read_le32(bytes + 12) != 0 ||
       read_le32(bytes + DIRECTORY_HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, DIRECTORY_HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    for(index = DIRECTORY_RESERVED_OFFSET;
        index < DIRECTORY_HEADER_CRC_OFFSET; ++index) {
        if(bytes[index]) {
            errno = EILSEQ;
            return -1;
        }
    }

    file_bytes = read_le32(bytes + 8);
    section_count = read_le32(bytes + DIRECTORY_SECTION_COUNT_OFFSET);
    directory_offset = read_le32(bytes + DIRECTORY_OFFSET_OFFSET);
    directory_bytes = read_le32(bytes + DIRECTORY_BYTES_OFFSET);
    if(file_bytes < PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES ||
       file_bytes > size || section_count < 2 ||
       directory_offset != PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES ||
       section_count >
           UINT32_MAX / PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES ||
       directory_bytes != (uint32_t)(
           section_count * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES) ||
       add_size(directory_offset, directory_bytes, &directory_end) < 0 ||
       directory_end > file_bytes ||
       read_le32(bytes + DIRECTORY_CRC_OFFSET) !=
           crc32_bytes(bytes + directory_offset, directory_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.section_directory = bytes + directory_offset;
    parsed.section_directory_bytes = directory_bytes;
    parsed.section_count = section_count;
    parsed.version = PVR_CHUNK_ASSET_DIRECTORY_VERSION;
    parsed.header_bytes = PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
    parsed.center[0] = read_le_float(bytes + 16);
    parsed.center[1] = read_le_float(bytes + 20);
    parsed.center[2] = read_le_float(bytes + 24);
    parsed.radius = read_le_float(bytes + 28);
    if(!isfinite(parsed.center[0]) || !isfinite(parsed.center[1]) ||
       !isfinite(parsed.center[2]) || !isfinite(parsed.radius) ||
       parsed.radius < 0.0f) {
        errno = EILSEQ;
        return -1;
    }

    previous_end = directory_end;
    for(index = 0; index < section_count; ++index) {
        const uint8_t *descriptor = bytes + directory_offset +
            index * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
        size_t section_offset;
        size_t section_end;

        if(parse_pcm2_section(bytes, file_bytes, descriptor, directory_end,
                              &section, &section_offset, &section_end) < 0)
            return -1;
        if(section_offset < previous_end) {
            errno = EILSEQ;
            return -1;
        }
        previous_end = section_end;

        if(section.type == PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM) {
            if(section.alignment < sizeof(uint32_t) ||
               section.decoded_bytes % sizeof(uint32_t)) {
                errno = EILSEQ;
                return -1;
            }
            if(!vertex_count)
                parsed.vertex = section;
            ++vertex_count;
        }
        else if(section.type == PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM) {
            if(section.alignment < sizeof(uint16_t) ||
               section.decoded_bytes % sizeof(uint16_t)) {
                errno = EILSEQ;
                return -1;
            }
            if(!polygon_count)
                parsed.polygon = section;
            ++polygon_count;
        }
    }
    if(!vertex_count || !polygon_count) {
        errno = EILSEQ;
        return -1;
    }
    parsed.model_count = vertex_count < polygon_count ? vertex_count :
                                                        polygon_count;

    *view = parsed;
    return 0;
}

int pvr_chunk_asset_open(const void *data, size_t size,
                         pvr_chunk_asset_view_t *view) {
    uint32_t magic;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < sizeof(uint32_t)) {
        errno = EILSEQ;
        return -1;
    }

    magic = read_le32(data);
    if(magic == PVR_CHUNK_ASSET_MAGIC)
        return open_pcm1(data, size, view);
    if(magic == PVR_CHUNK_ASSET_DIRECTORY_MAGIC)
        return open_pcm2(data, size, view);
    errno = EILSEQ;
    return -1;
}

int pvr_chunk_asset_section_get(const pvr_chunk_asset_view_t *view,
                                size_t index,
                                pvr_chunk_asset_section_t *section) {
    pvr_chunk_asset_view_t checked;
    size_t offset;
    size_t end;
    size_t minimum_offset;

    if(section)
        memset(section, 0, sizeof(*section));
    if(!view || !section || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0)
        return -1;
    if(index >= checked.section_count) {
        errno = ENOENT;
        return -1;
    }
    if(checked.version == PVR_CHUNK_ASSET_VERSION) {
        *section = index ? checked.polygon : checked.vertex;
        return 0;
    }

    minimum_offset = (const uint8_t *)checked.section_directory -
                     (const uint8_t *)checked.data +
                     checked.section_directory_bytes;
    return parse_pcm2_section(
        checked.data, checked.size,
        (const uint8_t *)checked.section_directory +
            index * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
        minimum_offset, section, &offset, &end);
}

int pvr_chunk_asset_section_find(const pvr_chunk_asset_view_t *view,
                                 uint32_t type, size_t ordinal,
                                 pvr_chunk_asset_section_t *section) {
    pvr_chunk_asset_view_t checked;
    size_t index;

    if(section)
        memset(section, 0, sizeof(*section));
    if(!view || !section || !view->data || !type) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0)
        return -1;
    for(index = 0; index < checked.section_count; ++index) {
        pvr_chunk_asset_section_t candidate;

        if(checked.version == PVR_CHUNK_ASSET_VERSION)
            candidate = index ? checked.polygon : checked.vertex;
        else {
            size_t offset;
            size_t end;
            size_t minimum_offset =
                (const uint8_t *)checked.section_directory -
                (const uint8_t *)checked.data +
                checked.section_directory_bytes;

            if(parse_pcm2_section(
                   checked.data, checked.size,
                   (const uint8_t *)checked.section_directory +
                       index * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                   minimum_offset, &candidate, &offset, &end) < 0)
                return -1;
        }
        if(candidate.type == type) {
            if(!ordinal) {
                *section = candidate;
                return 0;
            }
            --ordinal;
        }
    }

    errno = ENOENT;
    return -1;
}

static int section_needs_copy(const pvr_chunk_asset_section_t *section,
                              size_t natural_alignment) {
    return section->codec != PVR_CHUNK_ASSET_CODEC_RAW ||
           ((uintptr_t)section->stored_data & (natural_alignment - 1u));
}

int pvr_chunk_asset_pair_workspace_query(
    const pvr_chunk_asset_view_t *view, size_t vertex_ordinal,
    size_t polygon_ordinal,
    pvr_chunk_asset_workspace_requirements_t *requirements) {
    pvr_chunk_asset_view_t checked;
    pvr_chunk_asset_section_t vertex;
    pvr_chunk_asset_section_t polygon;
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
    if(pvr_chunk_asset_section_find(
           &checked, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
           vertex_ordinal, &vertex) < 0 ||
       pvr_chunk_asset_section_find(
           &checked, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
           polygon_ordinal, &polygon) < 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.alignment = PVR_CHUNK_ASSET_ALIGNMENT;
    result.copies_vertex = section_needs_copy(&vertex, vertex.alignment);
    result.copies_polygon = section_needs_copy(&polygon, polygon.alignment);
    if(result.copies_vertex) {
        if(align_size(cursor, result.alignment, &result.vertex_offset) < 0 ||
           add_size(result.vertex_offset, vertex.decoded_bytes,
                    &cursor) < 0)
            return -1;
    }
    if(result.copies_polygon) {
        if(align_size(cursor, result.alignment, &result.polygon_offset) < 0 ||
           add_size(result.polygon_offset, polygon.decoded_bytes,
                    &cursor) < 0)
            return -1;
    }
    result.bytes = cursor;
    *requirements = result;
    return 0;
}

int pvr_chunk_asset_model_workspace_query(
    const pvr_chunk_asset_view_t *view, size_t model_ordinal,
    pvr_chunk_asset_workspace_requirements_t *requirements) {
    pvr_chunk_asset_view_t checked;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!view || !requirements || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0)
        return -1;
    if(model_ordinal >= checked.model_count) {
        errno = ENOENT;
        return -1;
    }
    return pvr_chunk_asset_pair_workspace_query(
        &checked, model_ordinal, model_ordinal, requirements);
}

int pvr_chunk_asset_workspace_query(
    const pvr_chunk_asset_view_t *view,
    pvr_chunk_asset_workspace_requirements_t *requirements) {
    return pvr_chunk_asset_model_workspace_query(
        view, 0, requirements);
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

int pvr_chunk_asset_section_workspace_query(
    const pvr_chunk_asset_view_t *view, size_t index,
    pvr_chunk_asset_section_workspace_requirements_t *requirements) {
    pvr_chunk_asset_section_workspace_requirements_t result;
    pvr_chunk_asset_section_t section;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!view || !requirements || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_section_get(view, index, &section) < 0)
        return -1;

    memset(&result, 0, sizeof(result));
    result.alignment = PVR_CHUNK_ASSET_ALIGNMENT;
    result.copies = section_needs_copy(&section, section.alignment);
    if(result.copies)
        result.bytes = section.decoded_bytes;
    *requirements = result;
    return 0;
}

int pvr_chunk_asset_section_load(
    const pvr_chunk_asset_view_t *view, size_t index,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes, const void **decoded) {
    pvr_chunk_asset_section_workspace_requirements_t requirements;
    pvr_chunk_asset_section_t section;
    const void *result;

    if(decoded)
        *decoded = NULL;
    if(!view || !decoded || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_section_get(view, index, &section) < 0 ||
       pvr_chunk_asset_section_workspace_query(
           view, index, &requirements) < 0)
        return -1;
    if(requirements.bytes &&
       (!workspace || workspace_bytes < requirements.bytes ||
        ((uintptr_t)workspace & (requirements.alignment - 1u)))) {
        errno = workspace && workspace_bytes < requirements.bytes ? ENOSPC :
                                                                    EINVAL;
        return -1;
    }
    if(requirements.bytes &&
       ranges_overlap(workspace, requirements.bytes,
                      view->data, view->size)) {
        errno = EINVAL;
        return -1;
    }
    if(load_section(&section, requirements.copies, 0, decoder, decoder_data,
                    workspace, &result) < 0)
        return -1;

    *decoded = result;
    return 0;
}

int pvr_chunk_asset_pair_load(
    const pvr_chunk_asset_view_t *view, size_t vertex_ordinal,
    size_t polygon_ordinal,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *model_view) {
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_asset_view_t checked;
    pvr_chunk_asset_section_t vertex_section;
    pvr_chunk_asset_section_t polygon_section;
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
       pvr_chunk_asset_pair_workspace_query(
           &checked, vertex_ordinal, polygon_ordinal, &requirements) < 0 ||
       pvr_chunk_asset_section_find(
           &checked, PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
           vertex_ordinal, &vertex_section) < 0 ||
       pvr_chunk_asset_section_find(
           &checked, PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
           polygon_ordinal, &polygon_section) < 0)
        return -1;
    if(requirements.bytes &&
       (!workspace || workspace_bytes < requirements.bytes ||
        ((uintptr_t)workspace & (requirements.alignment - 1u)))) {
        errno = workspace && workspace_bytes < requirements.bytes ? ENOSPC :
                                                                    EINVAL;
        return -1;
    }
    if(requirements.bytes &&
       ranges_overlap(workspace, requirements.bytes,
                      checked.data, checked.size)) {
        errno = EINVAL;
        return -1;
    }

    if(load_section(&vertex_section, requirements.copies_vertex,
                    requirements.vertex_offset, decoder, decoder_data,
                    workspace, &vertex) < 0 ||
       load_section(&polygon_section, requirements.copies_polygon,
                    requirements.polygon_offset, decoder, decoder_data,
                    workspace, &polygon) < 0)
        return -1;

    model.vertex_words = vertex;
    model.vertex_word_count = vertex_section.decoded_bytes /
                              sizeof(uint32_t);
    model.polygon_words = polygon;
    model.polygon_word_count = polygon_section.decoded_bytes /
                               sizeof(uint16_t);
    memcpy(model.center, checked.center, sizeof(model.center));
    model.radius = checked.radius;
    return pvr_chunk_model_open(&model, model_view);
}

int pvr_chunk_asset_model_load(
    const pvr_chunk_asset_view_t *view, size_t model_ordinal,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *model_view) {
    pvr_chunk_asset_view_t checked;

    if(model_view)
        memset(model_view, 0, sizeof(*model_view));
    if(!view || !model_view || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_asset_open(view->data, view->size, &checked) < 0)
        return -1;
    if(model_ordinal >= checked.model_count) {
        errno = ENOENT;
        return -1;
    }
    return pvr_chunk_asset_pair_load(
        &checked, model_ordinal, model_ordinal, decoder, decoder_data,
        workspace, workspace_bytes, model_view);
}

int pvr_chunk_asset_load(const pvr_chunk_asset_view_t *view,
                         pvr_chunk_asset_decoder_t decoder,
                         void *decoder_data, void *workspace,
                         size_t workspace_bytes,
                         pvr_chunk_model_view_t *model_view) {
    return pvr_chunk_asset_model_load(
        view, 0, decoder, decoder_data, workspace, workspace_bytes,
        model_view);
}
