/* KallistiOS ##version##

   dc/pvr/pvr_chunk_cache_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_cache_asset.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "pvr_chunk_render_internal.h"

/* PCC1 mirrors the information in a published draw cache, not its native
   layout. Fixed-size descriptors and separate canonical arrays keep host
   pointer width, structure padding, and runtime texture addresses out of
   persistent assets. */
enum {
    HEADER_KIND_OFFSET = 12,
    HEADER_FORMAT_OFFSET = 14,
    HEADER_PRIMARY_COUNT_OFFSET = 16,
    HEADER_SECONDARY_COUNT_OFFSET = 20,
    HEADER_TERTIARY_COUNT_OFFSET = 24,
    HEADER_USER_WORD_COUNT_OFFSET = 28,
    HEADER_DESCRIPTOR_OFFSET = 32,
    HEADER_VERTEX_OFFSET = 36,
    HEADER_DEFORM_OFFSET = 40,
    HEADER_INDEX_OFFSET = 44,
    HEADER_USER_OFFSET = 48,
    HEADER_PAYLOAD_BYTES_OFFSET = 52,
    HEADER_CENTER_OFFSET = 56,
    HEADER_RADIUS_OFFSET = 68,
    HEADER_DESCRIPTOR_BYTES_OFFSET = 72,
    HEADER_VERTEX_BYTES_OFFSET = 74,
    HEADER_DEFORM_BYTES_OFFSET = 76,
    HEADER_INDEX_BYTES_OFFSET = 78,
    HEADER_USER_BYTES_OFFSET = 80,
    HEADER_FLAGS_OFFSET = 82,
    HEADER_PAYLOAD_CRC_OFFSET = 84,
    HEADER_RESERVED_OFFSET = 88,
    HEADER_CRC_OFFSET = 124,
    HEADER_CRC_BYTES = 124,
    STRIP_STATE_BYTES = 96
};

typedef struct native_layout {
    size_t descriptors_offset;
    size_t vertices_offset;
    size_t deform_offset;
    size_t indices_offset;
    size_t user_offset;
    size_t bytes;
    size_t vertex_size;
} native_layout_t;

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void write_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    write_le32(bytes, word);
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

static int add_size(size_t left, size_t right, size_t *result) {
    if(left > SIZE_MAX - right) {
        errno = ERANGE;
        return -1;
    }
    *result = left + right;
    return 0;
}

static int multiply_size(size_t count, size_t size, size_t *result) {
    if(size && count > SIZE_MAX / size) {
        errno = ERANGE;
        return -1;
    }
    *result = count * size;
    return 0;
}

static int align_size(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1u;

    if(value > SIZE_MAX - mask) {
        errno = ERANGE;
        return -1;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

static int bytes_are_zero(const uint8_t *bytes, size_t size) {
    size_t index;

    for(index = 0; index < size; ++index) {
        if(bytes[index])
            return 0;
    }
    return 1;
}

static size_t wire_vertex_size(pvr_chunk_cache_section_kind_t kind,
                               pvr_geometry_vertex_format_t format) {
    if(kind == PVR_CHUNK_CACHE_SECTION_ORDINARY)
        return sizeof(pvr_vertex_t);
    if(kind == PVR_CHUNK_CACHE_SECTION_MODIFIER)
        return sizeof(pvr_modifier_vol_t);
    if(kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME)
        return pvr_chunk_render_two_volume_format_size(format);
    return 0;
}

static int wire_layout(pvr_chunk_cache_section_kind_t kind,
                       pvr_geometry_vertex_format_t format,
                       size_t primary, size_t secondary, size_t tertiary,
                       size_t user_words, native_layout_t *layout) {
    size_t cursor = PVR_CHUNK_CACHE_SECTION_HEADER_BYTES;
    size_t bytes;
    size_t descriptor_count;
    size_t descriptor_size;
    size_t deform_count;

    /* Parser and serializer share this sole layout calculator. Requiring all
       stored offsets to match it rejects aliases and hidden payloads. */
    memset(layout, 0, sizeof(*layout));
    layout->vertex_size = wire_vertex_size(kind, format);
    if(!layout->vertex_size) {
        errno = EILSEQ;
        return -1;
    }
    descriptor_count = kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                       secondary : primary;
    descriptor_size = kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                      PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES :
                      PVR_CHUNK_CACHE_SECTION_STRIP_BYTES;
    deform_count = kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                   tertiary : secondary;
    layout->descriptors_offset = cursor;
    if(multiply_size(descriptor_count, descriptor_size, &bytes) < 0 ||
       add_size(cursor, bytes, &cursor) < 0 ||
       align_size(cursor, 32u, &layout->vertices_offset) < 0 ||
       multiply_size(secondary, layout->vertex_size, &bytes) < 0 ||
       add_size(layout->vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, 32u, &layout->deform_offset) < 0 ||
       multiply_size(deform_count, sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       add_size(layout->deform_offset, bytes, &cursor) < 0 ||
       align_size(cursor, sizeof(uint16_t), &layout->indices_offset) < 0 ||
       multiply_size(deform_count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(layout->indices_offset, bytes, &cursor) < 0)
        return -1;
    if(kind == PVR_CHUNK_CACHE_SECTION_MODIFIER) {
        if(align_size(cursor, sizeof(uint16_t), &layout->user_offset) < 0 ||
           multiply_size(user_words, sizeof(uint16_t), &bytes) < 0 ||
           add_size(layout->user_offset, bytes, &cursor) < 0)
            return -1;
    }
    if(align_size(cursor, 32u, &layout->bytes) < 0)
        return -1;
    return 0;
}

static int native_layout_get(const pvr_chunk_cache_section_view_t *view,
                             native_layout_t *layout) {
    size_t cursor;
    size_t count;
    size_t descriptor_size;
    size_t bytes;

    /* Packet regions stay 32-byte aligned for the TA; compact indices and
       user words need only their natural alignment. */
    memset(layout, 0, sizeof(*layout));
    layout->vertex_size = wire_vertex_size(view->kind, view->format);
    count = view->kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
            view->secondary_count : view->primary_count;
    descriptor_size = view->kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                      sizeof(pvr_chunk_cached_modifier_triangle_t) :
                      sizeof(pvr_chunk_cached_strip_t);
    layout->descriptors_offset = 0;
    if(multiply_size(count, descriptor_size, &cursor) < 0 ||
       align_size(cursor, 32u, &layout->vertices_offset) < 0 ||
       multiply_size(view->secondary_count, layout->vertex_size, &bytes) < 0 ||
       add_size(layout->vertices_offset, bytes, &cursor) < 0 ||
       align_size(cursor, 32u, &layout->deform_offset) < 0)
        return -1;
    count = view->kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
            view->tertiary_count : view->secondary_count;
    if(multiply_size(count, sizeof(pvr_deform_vertex_t), &bytes) < 0 ||
       add_size(layout->deform_offset, bytes, &cursor) < 0 ||
       align_size(cursor, sizeof(uint16_t), &layout->indices_offset) < 0 ||
       multiply_size(count, sizeof(uint16_t), &bytes) < 0 ||
       add_size(layout->indices_offset, bytes, &cursor) < 0)
        return -1;
    if(view->kind == PVR_CHUNK_CACHE_SECTION_MODIFIER) {
        if(align_size(cursor, sizeof(uint16_t), &layout->user_offset) < 0 ||
           multiply_size(view->user_word_count, sizeof(uint16_t), &bytes) < 0 ||
           add_size(layout->user_offset, bytes, &cursor) < 0)
            return -1;
    }
    return align_size(cursor, 32u, &layout->bytes);
}

static void decode_texture(const uint8_t *bytes,
                           pvr_chunk_texture_state_t *texture) {
    texture->identifier = read_le16(bytes);
    texture->filter = bytes[2];
    texture->supersample = bytes[3];
    texture->uv_flip = bytes[4];
    texture->uv_clamp = bytes[5];
    texture->mipmap_adjust = bytes[6];
}

static int texture_valid(const uint8_t *bytes) {
    return read_le16(bytes) <= PVR_CHUNK_TEXTURE_IDENTIFIER_MAX &&
           bytes[2] <= 3u && bytes[3] <= 1u && bytes[4] <= 3u &&
           bytes[5] <= 3u && bytes[6] <= 15u && !bytes[7];
}

static void decode_vector(const uint8_t *bytes, vector_t *vector) {
    vector->x = read_float(bytes);
    vector->y = read_float(bytes + 4);
    vector->z = read_float(bytes + 8);
    vector->w = read_float(bytes + 12);
}

static int vector_valid(const uint8_t *bytes, float expected_w) {
    return isfinite(read_float(bytes)) && isfinite(read_float(bytes + 4)) &&
           isfinite(read_float(bytes + 8)) &&
           read_float(bytes + 12) == expected_w;
}

static void decode_state(const uint8_t *bytes,
                         pvr_chunk_render_state_t *state) {
    memset(state, 0, sizeof(*state));
    state->present = read_le32(bytes);
    state->blend_source = (pvr_blend_mode_t)read_le32(bytes + 4);
    state->blend_destination = (pvr_blend_mode_t)read_le32(bytes + 8);
    state->mipmap_adjust = bytes[12];
    state->specular_exponent = bytes[13];
    state->strip_flags = bytes[14];
    decode_texture(bytes + 16, &state->texture);
    state->diffuse_argb = read_le32(bytes + 24);
    state->ambient_argb = read_le32(bytes + 28);
    state->specular_argb = read_le32(bytes + 32);
    decode_vector(bytes + 36, &state->bump_direction);
    decode_vector(bytes + 52, &state->bump_up);
    state->secondary_present = read_le32(bytes + 68);
    state->secondary_specular_exponent = bytes[72];
    decode_texture(bytes + 76, &state->secondary_texture);
    state->secondary_diffuse_argb = read_le32(bytes + 84);
    state->secondary_ambient_argb = read_le32(bytes + 88);
    state->secondary_specular_argb = read_le32(bytes + 92);
}

static int state_valid(const uint8_t *bytes) {
    const uint32_t known = PVR_CHUNK_RENDER_BLEND |
        PVR_CHUNK_RENDER_MIPMAP_ADJUST |
        PVR_CHUNK_RENDER_SPECULAR_EXPONENT |
        PVR_CHUNK_RENDER_TEXTURE | PVR_CHUNK_RENDER_DIFFUSE |
        PVR_CHUNK_RENDER_AMBIENT | PVR_CHUNK_RENDER_SPECULAR |
        PVR_CHUNK_RENDER_BUMP_BASIS;

    /* Texture state carries stable identifiers only. Existing binding
       callbacks reconstruct surfaces and other runtime material ownership. */
    return !(read_le32(bytes) & ~known) &&
           !(read_le32(bytes + 68) & ~known) &&
           read_le32(bytes + 4) <= PVR_BLEND_INVDESTALPHA &&
           read_le32(bytes + 8) <= PVR_BLEND_INVDESTALPHA &&
           bytes[12] <= 15u && !bytes[15] &&
           texture_valid(bytes + 16) &&
           vector_valid(bytes + 36, 0.0f) &&
           vector_valid(bytes + 52, 0.0f) &&
           !bytes[73] && !bytes[74] && !bytes[75] &&
           texture_valid(bytes + 76);
}

static int packet_valid(const uint8_t *bytes, size_t bytes_per_vertex,
                        pvr_chunk_cache_section_kind_t kind) {
    uint32_t command = read_le32(bytes);
    size_t float_words;
    size_t index;

    if((kind == PVR_CHUNK_CACHE_SECTION_MODIFIER &&
        command != PVR_CMD_VERTEX_EOL) ||
       (kind != PVR_CHUNK_CACHE_SECTION_MODIFIER &&
        command != PVR_CMD_VERTEX && command != PVR_CMD_VERTEX_EOL))
        return 0;
    if(kind == PVR_CHUNK_CACHE_SECTION_MODIFIER)
        float_words = 9u;
    else if(kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME &&
            bytes_per_vertex == sizeof(pvr_vertex_pcm_t))
        float_words = 3u;
    else if(kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME)
        float_words = 7u;
    else
        float_words = 5u;

    for(index = 0; index < float_words; ++index) {
        size_t word_index;

        if(kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME &&
           bytes_per_vertex == sizeof(pvr_vertex_tpcm_t) && index >= 5u)
            word_index = 8u + (index - 5u);
        else
            word_index = 1u + index;
        if(!isfinite(read_float(bytes + word_index * 4u)))
            return 0;
    }
    return 1;
}

static int deform_valid(const uint8_t *bytes) {
    return vector_valid(bytes, 1.0f) && vector_valid(bytes + 16, 0.0f);
}

static int strip_type_valid(uint8_t type,
                            pvr_chunk_cache_section_kind_t kind,
                            pvr_geometry_vertex_format_t format) {
    int two_volume = type == PVR_CHUNK_STRIP_TWO_VOLUME ||
        type == PVR_CHUNK_STRIP_UV8_TWO_VOLUME ||
        type == PVR_CHUNK_STRIP_UV10_TWO_VOLUME ||
        type == PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME ||
        type == PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME ||
        type == PVR_CHUNK_STRIP_UV_FLOAT_TWO_VOLUME;

    if(kind == PVR_CHUNK_CACHE_SECTION_ORDINARY)
        return type >= PVR_CHUNK_STRIP_INDEX &&
               type <= PVR_CHUNK_STRIP_UV_FLOAT_TWO_VOLUME && !two_volume;
    return two_volume &&
           pvr_chunk_render_two_volume_strip_format(type) == format;
}

static int validate_strips(const uint8_t *bytes, size_t strip_count,
                           size_t vertex_count, size_t maximum,
                           pvr_chunk_cache_section_kind_t kind,
                           pvr_geometry_vertex_format_t format) {
    size_t cursor = 0;
    size_t observed_maximum = 0;
    size_t index;

    for(index = 0; index < strip_count; ++index) {
        const uint8_t *record = bytes +
            index * PVR_CHUNK_CACHE_SECTION_STRIP_BYTES;
        size_t first = read_le32(record + 96);
        size_t count = read_le32(record + 100);
        uint8_t source_type = record[136];
        uint8_t source_flags = record[137];

        if(!state_valid(record) || first != cursor || count < 3u ||
           count > vertex_count - cursor ||
           !strip_type_valid(source_type, kind, format) ||
           source_flags & UINT8_C(0x80) || record[14] != source_flags ||
           !vector_valid(record + 104, 1.0f) ||
           !vector_valid(record + 120, 1.0f) ||
           read_float(record + 104) > read_float(record + 120) ||
           read_float(record + 108) > read_float(record + 124) ||
           read_float(record + 112) > read_float(record + 128) ||
           read_le16(record + 138) ||
           !bytes_are_zero(record + 140, 20u)) {
            errno = EILSEQ;
            return -1;
        }
        cursor += count;
        if(count > observed_maximum)
            observed_maximum = count;
    }
    if(cursor != vertex_count || observed_maximum != maximum) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int validate_modifiers(const uint8_t *bytes, size_t volumes,
                              size_t triangles, size_t corners,
                              size_t user_words) {
    size_t corner_cursor = 0;
    size_t word_cursor = 0;
    size_t observed_volumes = 0;
    size_t index;

    if(triangles > SIZE_MAX / 3u || corners != triangles * 3u)
        goto invalid;
    for(index = 0; index < triangles; ++index) {
        const uint8_t *record = bytes +
            index * PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES;
        size_t count = read_le32(record + 8);

        if(read_le32(record) != corner_cursor ||
           read_le32(record + 4) != word_cursor ||
           count > user_words - word_cursor ||
           record[12] < PVR_CHUNK_VOLUME_TRIANGLES ||
           record[12] > PVR_CHUNK_VOLUME_STRIPS || record[13] > 1u ||
           read_le16(record + 14) ||
           !bytes_are_zero(record + 16, 16u))
            goto invalid;
        corner_cursor += 3u;
        word_cursor += count;
        if(record[13])
            ++observed_volumes;
    }
    if(corner_cursor != corners || word_cursor != user_words ||
       observed_volumes != volumes || !bytes[(triangles - 1u) *
           PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES + 13u])
        goto invalid;
    return 0;

invalid:
    errno = EILSEQ;
    return -1;
}

static int checked_view(const pvr_chunk_cache_section_view_t *view,
                        pvr_chunk_cache_section_view_t *checked) {
    if(!view || !checked || !view->data) {
        errno = EINVAL;
        return -1;
    }
    return pvr_chunk_cache_section_open(view->data, view->size, checked);
}

int pvr_chunk_cache_section_open(
    const void *data, size_t size, pvr_chunk_cache_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_cache_section_view_t parsed;
    pvr_chunk_cache_section_kind_t kind;
    pvr_geometry_vertex_format_t format;
    native_layout_t layout;
    size_t primary;
    size_t secondary;
    size_t tertiary;
    size_t user_words;
    size_t deform_count;
    size_t index;

    /* Do not publish partial state: every framing, topology, packet, and
       numerical check completes into a local descriptor first. */
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_CACHE_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_CACHE_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_CACHE_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_CACHE_SECTION_HEADER_BYTES ||
       read_le32(bytes + 8) != size ||
       read_le16(bytes + HEADER_FLAGS_OFFSET) ||
       !bytes_are_zero(bytes + HEADER_RESERVED_OFFSET, 36u) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    kind = (pvr_chunk_cache_section_kind_t)read_le16(
        bytes + HEADER_KIND_OFFSET);
    format = (pvr_geometry_vertex_format_t)read_le16(
        bytes + HEADER_FORMAT_OFFSET);
    primary = read_le32(bytes + HEADER_PRIMARY_COUNT_OFFSET);
    secondary = read_le32(bytes + HEADER_SECONDARY_COUNT_OFFSET);
    tertiary = read_le32(bytes + HEADER_TERTIARY_COUNT_OFFSET);
    user_words = read_le32(bytes + HEADER_USER_WORD_COUNT_OFFSET);
    if(kind < PVR_CHUNK_CACHE_SECTION_ORDINARY ||
       kind > PVR_CHUNK_CACHE_SECTION_MODIFIER || !primary || !secondary ||
       (kind == PVR_CHUNK_CACHE_SECTION_ORDINARY &&
        (!tertiary || user_words || format)) ||
       (kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME &&
        (!tertiary || user_words)) ||
       (kind == PVR_CHUNK_CACHE_SECTION_TWO_VOLUME &&
        format != PVR_GEOMETRY_VERTEX_TWO_VOLUME_COLOR &&
        format != PVR_GEOMETRY_VERTEX_TWO_VOLUME_TEXTURED) ||
       (kind == PVR_CHUNK_CACHE_SECTION_MODIFIER &&
        (format || secondary > SIZE_MAX / 3u ||
         tertiary != secondary * 3u)) ||
       wire_layout(kind, format, primary, secondary, tertiary,
                   user_words, &layout) < 0 || layout.bytes != size ||
       read_le32(bytes + HEADER_DESCRIPTOR_OFFSET) !=
           layout.descriptors_offset ||
       read_le32(bytes + HEADER_VERTEX_OFFSET) != layout.vertices_offset ||
       read_le32(bytes + HEADER_DEFORM_OFFSET) != layout.deform_offset ||
       read_le32(bytes + HEADER_INDEX_OFFSET) != layout.indices_offset ||
       read_le32(bytes + HEADER_USER_OFFSET) != layout.user_offset ||
       read_le32(bytes + HEADER_PAYLOAD_BYTES_OFFSET) !=
           size - PVR_CHUNK_CACHE_SECTION_HEADER_BYTES ||
       read_le16(bytes + HEADER_DESCRIPTOR_BYTES_OFFSET) !=
           (kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
            PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES :
            PVR_CHUNK_CACHE_SECTION_STRIP_BYTES) ||
       read_le16(bytes + HEADER_VERTEX_BYTES_OFFSET) != layout.vertex_size ||
       read_le16(bytes + HEADER_DEFORM_BYTES_OFFSET) !=
           sizeof(pvr_deform_vertex_t) ||
       read_le16(bytes + HEADER_INDEX_BYTES_OFFSET) != sizeof(uint16_t) ||
       read_le16(bytes + HEADER_USER_BYTES_OFFSET) !=
           (kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ? sizeof(uint16_t) : 0u) ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_CACHE_SECTION_HEADER_BYTES,
           size - PVR_CHUNK_CACHE_SECTION_HEADER_BYTES)) {
        if(!errno)
            errno = EILSEQ;
        return -1;
    }
    if(kind == PVR_CHUNK_CACHE_SECTION_MODIFIER) {
        if(validate_modifiers(bytes + layout.descriptors_offset, primary,
                              secondary, tertiary, user_words) < 0)
            return -1;
    }
    else if(validate_strips(bytes + layout.descriptors_offset, primary,
                            secondary, tertiary, kind, format) < 0)
        return -1;

    for(index = 0; index < secondary; ++index) {
        if(!packet_valid(bytes + layout.vertices_offset +
                         index * layout.vertex_size,
                         layout.vertex_size, kind)) {
            errno = EILSEQ;
            return -1;
        }
    }
    deform_count = kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                   tertiary : secondary;
    for(index = 0; index < deform_count; ++index) {
        if(!deform_valid(bytes + layout.deform_offset +
                         index * sizeof(pvr_deform_vertex_t))) {
            errno = EILSEQ;
            return -1;
        }
    }
    if(!isfinite(read_float(bytes + HEADER_CENTER_OFFSET)) ||
       !isfinite(read_float(bytes + HEADER_CENTER_OFFSET + 4)) ||
       !isfinite(read_float(bytes + HEADER_CENTER_OFFSET + 8)) ||
       !isfinite(read_float(bytes + HEADER_RADIUS_OFFSET)) ||
       read_float(bytes + HEADER_RADIUS_OFFSET) < 0.0f) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = size;
    parsed.descriptors = bytes + layout.descriptors_offset;
    parsed.vertices = bytes + layout.vertices_offset;
    parsed.deform_vertices = bytes + layout.deform_offset;
    parsed.source_indices = bytes + layout.indices_offset;
    parsed.user_words = layout.user_offset ? bytes + layout.user_offset : NULL;
    parsed.primary_count = primary;
    parsed.secondary_count = secondary;
    parsed.tertiary_count = tertiary;
    parsed.user_word_count = user_words;
    parsed.center[0] = read_float(bytes + HEADER_CENTER_OFFSET);
    parsed.center[1] = read_float(bytes + HEADER_CENTER_OFFSET + 4);
    parsed.center[2] = read_float(bytes + HEADER_CENTER_OFFSET + 8);
    parsed.radius = read_float(bytes + HEADER_RADIUS_OFFSET);
    parsed.kind = kind;
    parsed.format = format;
    parsed.version = PVR_CHUNK_CACHE_SECTION_VERSION;
    *view = parsed;
    return 0;
}

int pvr_chunk_cache_section_workspace_query(
    const pvr_chunk_cache_section_view_t *view,
    pvr_chunk_cache_section_requirements_t *requirements) {
    pvr_chunk_cache_section_view_t checked;
    native_layout_t layout;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!requirements) {
        errno = EINVAL;
        return -1;
    }
    if(checked_view(view, &checked) < 0 ||
       native_layout_get(&checked, &layout) < 0)
        return -1;
    requirements->alignment = PVR_CHUNK_CACHE_ALIGNMENT;
    requirements->bytes = layout.bytes;
    requirements->kind = checked.kind;
    return 0;
}

static int ranges_overlap(const void *left, size_t left_bytes,
                          const void *right, size_t right_bytes) {
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if(!left_bytes || !right_bytes)
        return 0;
    if(left_bytes > UINTPTR_MAX - left_start ||
       right_bytes > UINTPTR_MAX - right_start)
        return 1;
    left_end = left_start + left_bytes;
    right_end = right_start + right_bytes;
    return left_start < right_end && right_start < left_end;
}

static int materialize_preflight(
    const pvr_chunk_cache_section_view_t *view,
    pvr_chunk_cache_section_kind_t kind, void *storage, size_t storage_bytes,
    const void *cache, size_t cache_bytes,
    pvr_chunk_cache_section_view_t *checked, native_layout_t *layout) {
    /* A view is only a convenience handle. Reopen its source bytes so a
       copied, stale, or caller-edited descriptor cannot bypass admission. */
    if(checked_view(view, checked) < 0)
        return -1;
    if(checked->kind != kind) {
        errno = EINVAL;
        return -1;
    }
    if(native_layout_get(checked, layout) < 0)
        return -1;
    if(!storage || ((uintptr_t)storage & 31u)) {
        errno = EINVAL;
        return -1;
    }
    if(storage_bytes < layout->bytes) {
        errno = ENOSPC;
        return -1;
    }
    if(ranges_overlap(checked->data, checked->size, storage, layout->bytes) ||
       ranges_overlap(cache, cache_bytes, storage, layout->bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static void decode_packet_words(const uint8_t *source, void *destination,
                                size_t bytes) {
    size_t index;

    /* Decode by word instead of casting wire bytes: the input may be
       unaligned and typed access would also violate strict aliasing. */
    for(index = 0; index < bytes / sizeof(uint32_t); ++index) {
        uint32_t word = read_le32(source + index * sizeof(uint32_t));

        memcpy((uint8_t *)destination + index * sizeof(uint32_t),
               &word, sizeof(word));
    }
}

static void decode_deformations(const pvr_chunk_cache_section_view_t *view,
                                pvr_deform_vertex_t *vertices, size_t count) {
    const uint8_t *source = view->deform_vertices;
    size_t index;

    for(index = 0; index < count; ++index) {
        decode_vector(source + index * 32u,
                      &vertices[index].position);
        decode_vector(source + index * 32u + 16u,
                      &vertices[index].normal);
    }
}

static void decode_indices(const pvr_chunk_cache_section_view_t *view,
                           uint16_t *indices, size_t count) {
    const uint8_t *source = view->source_indices;
    size_t index;

    for(index = 0; index < count; ++index)
        indices[index] = read_le16(source + index * sizeof(uint16_t));
}

static void decode_strip(const uint8_t *source,
                         pvr_chunk_cached_strip_t *strip) {
    memset(strip, 0, sizeof(*strip));
    decode_state(source, &strip->state);
    strip->first_vertex = read_le32(source + 96);
    strip->vertex_count = read_le32(source + 100);
    decode_vector(source + 104, &strip->minimum);
    decode_vector(source + 120, &strip->maximum);
    strip->source_type = source[136];
    strip->source_flags = source[137];
}

static void decode_strips(const pvr_chunk_cache_section_view_t *view,
                          pvr_chunk_cached_strip_t *strips) {
    const uint8_t *source = view->descriptors;
    size_t index;

    for(index = 0; index < view->primary_count; ++index)
        decode_strip(source + index * PVR_CHUNK_CACHE_SECTION_STRIP_BYTES,
                     strips + index);
}

int pvr_chunk_cache_section_materialize_ordinary(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes, pvr_chunk_model_cache_t *cache) {
    pvr_chunk_cache_section_view_t checked;
    native_layout_t layout;
    pvr_chunk_model_cache_t result;
    size_t index;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!cache || materialize_preflight(
           view, PVR_CHUNK_CACHE_SECTION_ORDINARY, storage, storage_bytes,
           cache, sizeof(*cache), &checked, &layout) < 0) {
        if(!cache)
            errno = EINVAL;
        return -1;
    }
    memset(storage, 0, layout.bytes);
    decode_strips(&checked, storage);
    for(index = 0; index < checked.secondary_count; ++index)
        decode_packet_words((const uint8_t *)checked.vertices + index * 32u,
            (uint8_t *)storage + layout.vertices_offset + index * 32u, 32u);
    decode_deformations(&checked,
        (pvr_deform_vertex_t *)((uint8_t *)storage + layout.deform_offset),
        checked.secondary_count);
    decode_indices(&checked,
        (uint16_t *)((uint8_t *)storage + layout.indices_offset),
        checked.secondary_count);

    memset(&result, 0, sizeof(result));
    result.version = PVR_CHUNK_CACHE_VERSION;
    result.storage = storage;
    result.storage_bytes = layout.bytes;
    result.strips = storage;
    result.strip_count = checked.primary_count;
    result.vertices = (const pvr_vertex_t *)((uint8_t *)storage +
                                             layout.vertices_offset);
    result.deform_vertices = (const pvr_deform_vertex_t *)(
        (uint8_t *)storage + layout.deform_offset);
    result.source_indices = (const uint16_t *)((uint8_t *)storage +
                                               layout.indices_offset);
    result.vertex_count = checked.secondary_count;
    result.maximum_strip_vertices = checked.tertiary_count;
    memcpy(result.center, checked.center, sizeof(result.center));
    result.radius = checked.radius;
    if(pvr_chunk_model_cache_validate(&result) < 0)
        return -1;
    *cache = result;
    return 0;
}

int pvr_chunk_cache_section_materialize_two_volume(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes,
    pvr_chunk_two_volume_cache_t *cache) {
    pvr_chunk_cache_section_view_t checked;
    native_layout_t layout;
    pvr_chunk_two_volume_cache_t result;
    size_t index;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!cache || materialize_preflight(
           view, PVR_CHUNK_CACHE_SECTION_TWO_VOLUME, storage, storage_bytes,
           cache, sizeof(*cache), &checked, &layout) < 0) {
        if(!cache)
            errno = EINVAL;
        return -1;
    }
    memset(storage, 0, layout.bytes);
    decode_strips(&checked, storage);
    for(index = 0; index < checked.secondary_count; ++index)
        decode_packet_words((const uint8_t *)checked.vertices +
            index * layout.vertex_size,
            (uint8_t *)storage + layout.vertices_offset +
            index * layout.vertex_size, layout.vertex_size);
    decode_deformations(&checked,
        (pvr_deform_vertex_t *)((uint8_t *)storage + layout.deform_offset),
        checked.secondary_count);
    decode_indices(&checked,
        (uint16_t *)((uint8_t *)storage + layout.indices_offset),
        checked.secondary_count);

    memset(&result, 0, sizeof(result));
    result.version = PVR_CHUNK_CACHE_VERSION;
    result.storage = storage;
    result.storage_bytes = layout.bytes;
    result.strips = storage;
    result.strip_count = checked.primary_count;
    result.vertices = (const uint8_t *)storage + layout.vertices_offset;
    result.deform_vertices = (const pvr_deform_vertex_t *)(
        (uint8_t *)storage + layout.deform_offset);
    result.source_indices = (const uint16_t *)((uint8_t *)storage +
                                               layout.indices_offset);
    result.vertex_count = checked.secondary_count;
    result.maximum_strip_vertices = checked.tertiary_count;
    result.format = checked.format;
    result.vertex_size = layout.vertex_size;
    memcpy(result.center, checked.center, sizeof(result.center));
    result.radius = checked.radius;
    if(pvr_chunk_model_two_volume_cache_validate(&result) < 0)
        return -1;
    *cache = result;
    return 0;
}

int pvr_chunk_cache_section_materialize_modifier(
    const pvr_chunk_cache_section_view_t *view,
    void *storage, size_t storage_bytes,
    pvr_chunk_modifier_cache_t *cache) {
    pvr_chunk_cache_section_view_t checked;
    native_layout_t layout;
    pvr_chunk_modifier_cache_t result;
    pvr_chunk_cached_modifier_triangle_t *triangles;
    size_t index;

    if(cache)
        memset(cache, 0, sizeof(*cache));
    if(!cache || materialize_preflight(
           view, PVR_CHUNK_CACHE_SECTION_MODIFIER, storage, storage_bytes,
           cache, sizeof(*cache), &checked, &layout) < 0) {
        if(!cache)
            errno = EINVAL;
        return -1;
    }
    memset(storage, 0, layout.bytes);
    triangles = storage;
    for(index = 0; index < checked.secondary_count; ++index) {
        const uint8_t *source = (const uint8_t *)checked.descriptors +
            index * PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES;

        triangles[index].first_corner = read_le32(source);
        triangles[index].first_user_word = read_le32(source + 4);
        triangles[index].user_word_count = read_le32(source + 8);
        triangles[index].source_type = source[12];
        triangles[index].final_in_volume = source[13];
        decode_packet_words((const uint8_t *)checked.vertices + index * 64u,
            (uint8_t *)storage + layout.vertices_offset + index * 64u, 64u);
    }
    decode_deformations(&checked,
        (pvr_deform_vertex_t *)((uint8_t *)storage + layout.deform_offset),
        checked.tertiary_count);
    decode_indices(&checked,
        (uint16_t *)((uint8_t *)storage + layout.indices_offset),
        checked.tertiary_count);
    for(index = 0; index < checked.user_word_count; ++index)
        ((uint16_t *)((uint8_t *)storage + layout.user_offset))[index] =
            read_le16((const uint8_t *)checked.user_words + index * 2u);

    memset(&result, 0, sizeof(result));
    result.version = PVR_CHUNK_CACHE_VERSION;
    result.storage = storage;
    result.storage_bytes = layout.bytes;
    result.triangles = triangles;
    result.packets = (const pvr_modifier_vol_t *)((uint8_t *)storage +
                                                  layout.vertices_offset);
    result.deform_vertices = (const pvr_deform_vertex_t *)(
        (uint8_t *)storage + layout.deform_offset);
    result.source_indices = (const uint16_t *)((uint8_t *)storage +
                                               layout.indices_offset);
    result.user_words = (const uint16_t *)((uint8_t *)storage +
                                           layout.user_offset);
    result.volume_count = checked.primary_count;
    result.triangle_count = checked.secondary_count;
    result.corner_count = checked.tertiary_count;
    result.user_word_count = checked.user_word_count;
    memcpy(result.center, checked.center, sizeof(result.center));
    result.radius = checked.radius;
    if(pvr_chunk_model_modifier_cache_validate(&result) < 0)
        return -1;
    *cache = result;
    return 0;
}

static void encode_texture(uint8_t *bytes,
                           const pvr_chunk_texture_state_t *texture) {
    write_le16(bytes, texture->identifier);
    bytes[2] = texture->filter;
    bytes[3] = texture->supersample;
    bytes[4] = texture->uv_flip;
    bytes[5] = texture->uv_clamp;
    bytes[6] = texture->mipmap_adjust;
}

static void encode_vector(uint8_t *bytes, const vector_t *vector) {
    write_float(bytes, vector->x);
    write_float(bytes + 4, vector->y);
    write_float(bytes + 8, vector->z);
    write_float(bytes + 12, vector->w);
}

static void encode_state(uint8_t *bytes,
                         const pvr_chunk_render_state_t *state) {
    write_le32(bytes, state->present);
    write_le32(bytes + 4, (uint32_t)state->blend_source);
    write_le32(bytes + 8, (uint32_t)state->blend_destination);
    bytes[12] = state->mipmap_adjust;
    bytes[13] = state->specular_exponent;
    bytes[14] = state->strip_flags;
    encode_texture(bytes + 16, &state->texture);
    write_le32(bytes + 24, state->diffuse_argb);
    write_le32(bytes + 28, state->ambient_argb);
    write_le32(bytes + 32, state->specular_argb);
    encode_vector(bytes + 36, &state->bump_direction);
    encode_vector(bytes + 52, &state->bump_up);
    write_le32(bytes + 68, state->secondary_present);
    bytes[72] = state->secondary_specular_exponent;
    encode_texture(bytes + 76, &state->secondary_texture);
    write_le32(bytes + 84, state->secondary_diffuse_argb);
    write_le32(bytes + 88, state->secondary_ambient_argb);
    write_le32(bytes + 92, state->secondary_specular_argb);
}

static void encode_strip(uint8_t *bytes,
                         const pvr_chunk_cached_strip_t *strip) {
    encode_state(bytes, &strip->state);
    write_le32(bytes + 96, (uint32_t)strip->first_vertex);
    write_le32(bytes + 100, (uint32_t)strip->vertex_count);
    encode_vector(bytes + 104, &strip->minimum);
    encode_vector(bytes + 120, &strip->maximum);
    bytes[136] = strip->source_type;
    bytes[137] = strip->source_flags;
}

static void encode_packet_words(uint8_t *destination, const void *source,
                                size_t bytes) {
    size_t index;

    for(index = 0; index < bytes / sizeof(uint32_t); ++index) {
        uint32_t word;

        memcpy(&word, (const uint8_t *)source +
                      index * sizeof(uint32_t), sizeof(word));
        write_le32(destination + index * sizeof(uint32_t), word);
    }
}

static void encode_deformations(uint8_t *destination,
                                const pvr_deform_vertex_t *vertices,
                                size_t count) {
    size_t index;

    for(index = 0; index < count; ++index) {
        encode_vector(destination + index * 32u, &vertices[index].position);
        encode_vector(destination + index * 32u + 16u,
                      &vertices[index].normal);
    }
}

static int encode_header(uint8_t *bytes, size_t size,
                         pvr_chunk_cache_section_kind_t kind,
                         pvr_geometry_vertex_format_t format,
                         size_t primary, size_t secondary, size_t tertiary,
                         size_t user_words, const float center[3], float radius,
                         const native_layout_t *layout) {
    if(size > UINT32_MAX || primary > UINT32_MAX || secondary > UINT32_MAX ||
       tertiary > UINT32_MAX || user_words > UINT32_MAX ||
       layout->descriptors_offset > UINT32_MAX ||
       layout->vertices_offset > UINT32_MAX ||
       layout->deform_offset > UINT32_MAX ||
       layout->indices_offset > UINT32_MAX ||
       layout->user_offset > UINT32_MAX || layout->vertex_size > UINT16_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    write_le32(bytes, PVR_CHUNK_CACHE_SECTION_MAGIC);
    write_le16(bytes + 4, PVR_CHUNK_CACHE_SECTION_VERSION);
    write_le16(bytes + 6, PVR_CHUNK_CACHE_SECTION_HEADER_BYTES);
    write_le32(bytes + 8, (uint32_t)size);
    write_le16(bytes + HEADER_KIND_OFFSET, (uint16_t)kind);
    write_le16(bytes + HEADER_FORMAT_OFFSET, (uint16_t)format);
    write_le32(bytes + HEADER_PRIMARY_COUNT_OFFSET, (uint32_t)primary);
    write_le32(bytes + HEADER_SECONDARY_COUNT_OFFSET, (uint32_t)secondary);
    write_le32(bytes + HEADER_TERTIARY_COUNT_OFFSET, (uint32_t)tertiary);
    write_le32(bytes + HEADER_USER_WORD_COUNT_OFFSET, (uint32_t)user_words);
    write_le32(bytes + HEADER_DESCRIPTOR_OFFSET,
               (uint32_t)layout->descriptors_offset);
    write_le32(bytes + HEADER_VERTEX_OFFSET, (uint32_t)layout->vertices_offset);
    write_le32(bytes + HEADER_DEFORM_OFFSET, (uint32_t)layout->deform_offset);
    write_le32(bytes + HEADER_INDEX_OFFSET, (uint32_t)layout->indices_offset);
    write_le32(bytes + HEADER_USER_OFFSET, (uint32_t)layout->user_offset);
    write_le32(bytes + HEADER_PAYLOAD_BYTES_OFFSET,
               (uint32_t)(size - PVR_CHUNK_CACHE_SECTION_HEADER_BYTES));
    write_float(bytes + HEADER_CENTER_OFFSET, center[0]);
    write_float(bytes + HEADER_CENTER_OFFSET + 4, center[1]);
    write_float(bytes + HEADER_CENTER_OFFSET + 8, center[2]);
    write_float(bytes + HEADER_RADIUS_OFFSET, radius);
    write_le16(bytes + HEADER_DESCRIPTOR_BYTES_OFFSET,
               kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
               PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES :
               PVR_CHUNK_CACHE_SECTION_STRIP_BYTES);
    write_le16(bytes + HEADER_VERTEX_BYTES_OFFSET,
               (uint16_t)layout->vertex_size);
    write_le16(bytes + HEADER_DEFORM_BYTES_OFFSET,
               sizeof(pvr_deform_vertex_t));
    write_le16(bytes + HEADER_INDEX_BYTES_OFFSET, sizeof(uint16_t));
    write_le16(bytes + HEADER_USER_BYTES_OFFSET,
               kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
               sizeof(uint16_t) : 0u);
    return 0;
}

static int serialize_cache(
    pvr_chunk_cache_section_kind_t kind,
    pvr_geometry_vertex_format_t format,
    const pvr_chunk_cached_strip_t *strips,
    const pvr_chunk_cached_modifier_triangle_t *triangles,
    const void *vertices, const pvr_deform_vertex_t *deform_vertices,
    const uint16_t *indices, const uint16_t *user_data,
    size_t primary, size_t secondary, size_t tertiary, size_t user_words,
    const float center[3], float radius,
    void *destination, size_t destination_bytes) {
    native_layout_t layout;
    uint8_t *bytes = destination;
    size_t deform_count = kind == PVR_CHUNK_CACHE_SECTION_MODIFIER ?
                          tertiary : secondary;
    pvr_chunk_cache_section_view_t checked;
    size_t index;

    /* Zeroing makes padding and every reserved field canonical and covered by
       the section checksums, rather than leaking arbitrary host bytes. */
    if(wire_layout(kind, format, primary, secondary, tertiary,
                   user_words, &layout) < 0)
        return -1;
    if(!destination) {
        errno = EINVAL;
        return -1;
    }
    if(destination_bytes < layout.bytes) {
        errno = ENOSPC;
        return -1;
    }
    memset(destination, 0, layout.bytes);
    if(encode_header(bytes, layout.bytes, kind, format, primary, secondary,
                     tertiary, user_words, center, radius, &layout) < 0)
        return -1;
    if(kind == PVR_CHUNK_CACHE_SECTION_MODIFIER) {
        for(index = 0; index < secondary; ++index) {
            uint8_t *record = bytes + layout.descriptors_offset +
                index * PVR_CHUNK_CACHE_SECTION_MODIFIER_BYTES;

            write_le32(record, (uint32_t)triangles[index].first_corner);
            write_le32(record + 4,
                       (uint32_t)triangles[index].first_user_word);
            write_le32(record + 8,
                       (uint32_t)triangles[index].user_word_count);
            record[12] = triangles[index].source_type;
            record[13] = triangles[index].final_in_volume;
        }
    }
    else {
        for(index = 0; index < primary; ++index)
            encode_strip(bytes + layout.descriptors_offset +
                         index * PVR_CHUNK_CACHE_SECTION_STRIP_BYTES,
                         strips + index);
    }
    for(index = 0; index < secondary; ++index)
        encode_packet_words(bytes + layout.vertices_offset +
            index * layout.vertex_size,
            (const uint8_t *)vertices + index * layout.vertex_size,
            layout.vertex_size);
    encode_deformations(bytes + layout.deform_offset,
                        deform_vertices, deform_count);
    for(index = 0; index < deform_count; ++index)
        write_le16(bytes + layout.indices_offset + index * 2u,
                   indices[index]);
    for(index = 0; index < user_words; ++index)
        write_le16(bytes + layout.user_offset + index * 2u,
                   user_data[index]);
    write_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET, crc32_bytes(
        bytes + PVR_CHUNK_CACHE_SECTION_HEADER_BYTES,
        layout.bytes - PVR_CHUNK_CACHE_SECTION_HEADER_BYTES));
    write_le32(bytes + HEADER_CRC_OFFSET, crc32_bytes(bytes, HEADER_CRC_BYTES));
    /* Re-admit the completed stream before exposing it. The parser therefore
       remains the single definition of valid PCC1 data. */
    return pvr_chunk_cache_section_open(destination, layout.bytes, &checked);
}

static int reject_serialization_overlap(
    const void *destination, size_t destination_bytes,
    const void *storage, size_t storage_bytes,
    const void *descriptor, size_t descriptor_bytes) {
    /* Serialization writes header, payload, then checksums in place. Source
       overlap could corrupt later reads even when one memcpy looked safe. */
    if(ranges_overlap(destination, destination_bytes,
                      storage, storage_bytes) ||
       ranges_overlap(destination, destination_bytes,
                      descriptor, descriptor_bytes)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_cache_section_query(
    const pvr_chunk_model_cache_t *cache, size_t *bytes) {
    native_layout_t layout;

    if(bytes)
        *bytes = 0;
    if(!bytes || pvr_chunk_model_cache_validate(cache) < 0 ||
       wire_layout(PVR_CHUNK_CACHE_SECTION_ORDINARY, 0,
                   cache->strip_count, cache->vertex_count,
                   cache->maximum_strip_vertices, 0, &layout) < 0) {
        if(!bytes)
            errno = EINVAL;
        return -1;
    }
    *bytes = layout.bytes;
    return 0;
}

int pvr_chunk_model_cache_section_serialize(
    const pvr_chunk_model_cache_t *cache, void *destination,
    size_t destination_bytes) {
    size_t bytes;

    if(pvr_chunk_model_cache_section_query(cache, &bytes) < 0 ||
       reject_serialization_overlap(
           destination, bytes, cache->storage, cache->storage_bytes,
           cache, sizeof(*cache)) < 0)
        return -1;
    return serialize_cache(PVR_CHUNK_CACHE_SECTION_ORDINARY, 0,
        cache->strips, NULL, cache->vertices, cache->deform_vertices,
        cache->source_indices, NULL, cache->strip_count, cache->vertex_count,
        cache->maximum_strip_vertices, 0, cache->center, cache->radius,
        destination, destination_bytes);
}

int pvr_chunk_two_volume_cache_section_query(
    const pvr_chunk_two_volume_cache_t *cache, size_t *bytes) {
    native_layout_t layout;

    if(bytes)
        *bytes = 0;
    if(!bytes || pvr_chunk_model_two_volume_cache_validate(cache) < 0 ||
       wire_layout(PVR_CHUNK_CACHE_SECTION_TWO_VOLUME, cache->format,
                   cache->strip_count, cache->vertex_count,
                   cache->maximum_strip_vertices, 0, &layout) < 0) {
        if(!bytes)
            errno = EINVAL;
        return -1;
    }
    *bytes = layout.bytes;
    return 0;
}

int pvr_chunk_two_volume_cache_section_serialize(
    const pvr_chunk_two_volume_cache_t *cache, void *destination,
    size_t destination_bytes) {
    size_t bytes;

    if(pvr_chunk_two_volume_cache_section_query(cache, &bytes) < 0 ||
       reject_serialization_overlap(
           destination, bytes, cache->storage, cache->storage_bytes,
           cache, sizeof(*cache)) < 0)
        return -1;
    return serialize_cache(PVR_CHUNK_CACHE_SECTION_TWO_VOLUME, cache->format,
        cache->strips, NULL, cache->vertices, cache->deform_vertices,
        cache->source_indices, NULL, cache->strip_count, cache->vertex_count,
        cache->maximum_strip_vertices, 0, cache->center, cache->radius,
        destination, destination_bytes);
}

int pvr_chunk_modifier_cache_section_query(
    const pvr_chunk_modifier_cache_t *cache, size_t *bytes) {
    native_layout_t layout;

    if(bytes)
        *bytes = 0;
    if(!bytes || pvr_chunk_model_modifier_cache_validate(cache) < 0 ||
       wire_layout(PVR_CHUNK_CACHE_SECTION_MODIFIER, 0,
                   cache->volume_count, cache->triangle_count,
                   cache->corner_count, cache->user_word_count,
                   &layout) < 0) {
        if(!bytes)
            errno = EINVAL;
        return -1;
    }
    *bytes = layout.bytes;
    return 0;
}

int pvr_chunk_modifier_cache_section_serialize(
    const pvr_chunk_modifier_cache_t *cache, void *destination,
    size_t destination_bytes) {
    size_t bytes;

    if(pvr_chunk_modifier_cache_section_query(cache, &bytes) < 0 ||
       reject_serialization_overlap(
           destination, bytes, cache->storage, cache->storage_bytes,
           cache, sizeof(*cache)) < 0)
        return -1;
    return serialize_cache(PVR_CHUNK_CACHE_SECTION_MODIFIER, 0, NULL,
        cache->triangles, cache->packets, cache->deform_vertices,
        cache->source_indices, cache->user_words, cache->volume_count,
        cache->triangle_count, cache->corner_count, cache->user_word_count,
        cache->center, cache->radius, destination, destination_bytes);
}
