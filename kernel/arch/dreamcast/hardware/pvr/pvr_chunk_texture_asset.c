/* KallistiOS ##version##

   dc/pvr/pvr_chunk_texture_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>
#include <dc/pvr_chunk_texture_asset.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_ENTRIES_CRC_OFFSET = 24,
    HEADER_DATA_CRC_OFFSET = 28,
    HEADER_RESERVED_OFFSET = 32,
    HEADER_CRC_OFFSET = 60,
    HEADER_CRC_BYTES = 60,
    ENTRY_RESERVED_OFFSET = 24
};

static const size_t mip_offset_16bpp[] = {
    0x00006, 0x00008, 0x00010, 0x00030, 0x000b0, 0x002b0,
    0x00ab0, 0x02ab0, 0x0aab0, 0x2aab0, 0xaaab0
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
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

static int power_of_two_dimension(uint32_t value) {
    return value >= 8u && value <= 1024u && !(value & (value - 1u));
}

static uint32_t bits_per_pixel(pvr_txr_surface_format_t format) {
    if(format == PVR_TXR_SURFACE_PAL4BPP)
        return 4u;
    if(format == PVR_TXR_SURFACE_PAL8BPP)
        return 8u;
    return 16u;
}

static uint32_t integer_log2(uint32_t value) {
    uint32_t result = 0;

    while(value > 1u) {
        value >>= 1;
        ++result;
    }
    return result;
}

static int expected_image_size(uint32_t width, uint32_t height,
                               pvr_txr_surface_format_t format,
                               pvr_txr_surface_layout_t layout,
                               int mipmapped, uint16_t codebook_entries,
                               size_t *byte_size) {
    uint32_t bpp;
    size_t top_size;
    size_t data_size;
    size_t codebook_size = 0;

    if(format < PVR_TXR_SURFACE_ARGB1555 ||
       format > PVR_TXR_SURFACE_PAL8BPP ||
       layout < PVR_TXR_SURFACE_TWIDDLED ||
       layout > PVR_TXR_SURFACE_VQ) {
        errno = EILSEQ;
        return -1;
    }
    if(layout == PVR_TXR_SURFACE_STRIDE) {
        if(width < 32u || width > 992u || (width & 31u) ||
           !power_of_two_dimension(height)) {
            errno = EILSEQ;
            return -1;
        }
    }
    else if(!power_of_two_dimension(width) ||
            !power_of_two_dimension(height)) {
        errno = EILSEQ;
        return -1;
    }
    if((format == PVR_TXR_SURFACE_PAL4BPP ||
        format == PVR_TXR_SURFACE_PAL8BPP) &&
       layout != PVR_TXR_SURFACE_TWIDDLED) {
        errno = EILSEQ;
        return -1;
    }
    if(layout == PVR_TXR_SURFACE_VQ) {
        if(format > PVR_TXR_SURFACE_BUMP || !codebook_entries ||
           codebook_entries > 256u) {
            errno = EILSEQ;
            return -1;
        }
        codebook_size = (size_t)codebook_entries * 8u;
    }
    else if(codebook_entries) {
        errno = EILSEQ;
        return -1;
    }
    if(mipmapped &&
       (width != height ||
        (layout != PVR_TXR_SURFACE_TWIDDLED &&
         layout != PVR_TXR_SURFACE_VQ))) {
        errno = EILSEQ;
        return -1;
    }

    bpp = bits_per_pixel(format);
    top_size = ((size_t)width * height * bpp + 7u) / 8u;
    if(layout == PVR_TXR_SURFACE_VQ)
        top_size = ((size_t)width * height * bpp + 63u) / 64u;
    data_size = top_size;
    if(mipmapped) {
        size_t offset = mip_offset_16bpp[integer_log2(width)];

        if(layout == PVR_TXR_SURFACE_VQ)
            offset /= 8u;
        else if(format == PVR_TXR_SURFACE_PAL4BPP)
            offset /= 4u;
        else if(format == PVR_TXR_SURFACE_PAL8BPP)
            offset /= 2u;
        data_size += offset;
    }
    *byte_size = codebook_size + data_size;
    return 0;
}

static int decode_entry(const uint8_t *base, const uint8_t *encoded,
                        pvr_chunk_texture_image_t *image) {
    uint16_t flags = read_le16(encoded + 8);
    uint32_t offset = read_le32(encoded + 12);
    uint32_t bytes = read_le32(encoded + 16);
    uint16_t codebook_entries = read_le16(encoded + 10);
    pvr_txr_surface_format_t format =
        (pvr_txr_surface_format_t)encoded[2];
    pvr_txr_surface_layout_t layout =
        (pvr_txr_surface_layout_t)encoded[3];
    size_t expected_size;

    if(flags & ~PVR_CHUNK_TEXTURE_IMAGE_MIPMAPPED) {
        errno = EILSEQ;
        return -1;
    }
    if(expected_image_size(
           read_le16(encoded + 4), read_le16(encoded + 6), format,
           layout, (flags & PVR_CHUNK_TEXTURE_IMAGE_MIPMAPPED) != 0,
           codebook_entries, &expected_size) < 0 ||
       expected_size != bytes) {
        errno = EILSEQ;
        return -1;
    }

    memset(image, 0, sizeof(*image));
    image->identifier = read_le16(encoded);
    image->codebook_entries = codebook_entries;
    image->width = read_le16(encoded + 4);
    image->height = read_le16(encoded + 6);
    image->format = format;
    image->layout = layout;
    image->mipmapped =
        (flags & PVR_CHUNK_TEXTURE_IMAGE_MIPMAPPED) != 0;
    image->data = base + offset;
    image->data_size = bytes;
    return 0;
}

static void entry_at(const pvr_chunk_texture_section_view_t *view,
                     size_t index, pvr_chunk_texture_image_t *image) {
    const uint8_t *encoded = (const uint8_t *)view->entries +
        index * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES;

    (void)decode_entry(view->data, encoded, image);
}

static int checked_view(const pvr_chunk_texture_section_view_t *view,
                        pvr_chunk_texture_section_view_t *checked) {
    if(!view || !checked || !view->data) {
        errno = EINVAL;
        return -1;
    }
    return pvr_chunk_texture_section_open(view->data, view->size, checked);
}

int pvr_chunk_texture_section_open(
        const void *data, size_t size,
        pvr_chunk_texture_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_texture_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t entry_count;
    uint32_t data_offset;
    size_t entries_bytes;
    size_t previous_end;
    uint16_t previous_identifier = 0;
    size_t index;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_TEXTURE_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_TEXTURE_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES ||
       read_le16(bytes + 16) != PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES ||
       read_le16(bytes + 18) ||
       memcmp(bytes + HEADER_RESERVED_OFFSET,
              (const uint8_t[HEADER_CRC_OFFSET - HEADER_RESERVED_OFFSET]){0},
              HEADER_CRC_OFFSET - HEADER_RESERVED_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    entry_count = read_le32(bytes + 12);
    data_offset = read_le32(bytes + 20);
    if(file_bytes != size || !entry_count) {
        errno = EILSEQ;
        return -1;
    }
    entries_bytes = (size_t)entry_count *
                    PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES;
    if(entries_bytes / PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES != entry_count ||
       data_offset < PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES ||
       entries_bytes > data_offset - PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES ||
       (data_offset & 31u) || data_offset >= file_bytes ||
       read_le32(bytes + HEADER_ENTRIES_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES, entries_bytes) ||
       read_le32(bytes + HEADER_DATA_CRC_OFFSET) != crc32_bytes(
           bytes + data_offset, file_bytes - data_offset)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.entries = bytes + PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES;
    parsed.entry_count = entry_count;
    parsed.version = PVR_CHUNK_TEXTURE_SECTION_VERSION;
    previous_end = data_offset;
    for(index = 0; index < entry_count; ++index) {
        const uint8_t *encoded = (const uint8_t *)parsed.entries +
            index * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES;
        uint16_t identifier = read_le16(encoded);
        uint32_t offset = read_le32(encoded + 12);
        uint32_t byte_count = read_le32(encoded + 16);
        pvr_chunk_texture_image_t image;

        if(identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX ||
           (index && identifier <= previous_identifier) ||
           memcmp(encoded + ENTRY_RESERVED_OFFSET,
                  (const uint8_t[PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES -
                                 ENTRY_RESERVED_OFFSET]){0},
                  PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES -
                      ENTRY_RESERVED_OFFSET) ||
           !byte_count || (offset & 31u) || offset < previous_end ||
           offset > file_bytes ||
           byte_count > file_bytes - offset ||
           read_le32(encoded + 20) != crc32_bytes(bytes + offset,
                                                  byte_count) ||
           decode_entry(bytes, encoded, &image) < 0) {
            errno = EILSEQ;
            return -1;
        }
        previous_identifier = identifier;
        previous_end = (size_t)offset + byte_count;
    }
    *view = parsed;
    return 0;
}

int pvr_chunk_texture_section_entry_get(
        const pvr_chunk_texture_section_view_t *view, size_t index,
        pvr_chunk_texture_image_t *image) {
    pvr_chunk_texture_section_view_t checked;

    if(image)
        memset(image, 0, sizeof(*image));
    if(!image) {
        errno = EINVAL;
        return -1;
    }
    if(checked_view(view, &checked) < 0)
        return -1;
    if(index >= checked.entry_count) {
        errno = ENOENT;
        return -1;
    }
    entry_at(&checked, index, image);
    return 0;
}

int pvr_chunk_texture_section_find(
        const pvr_chunk_texture_section_view_t *view, uint16_t identifier,
        pvr_chunk_texture_image_t *image) {
    pvr_chunk_texture_section_view_t checked;
    size_t lower = 0;
    size_t upper;

    if(image)
        memset(image, 0, sizeof(*image));
    if(!image) {
        errno = EINVAL;
        return -1;
    }
    if(identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX) {
        errno = ERANGE;
        return -1;
    }
    if(checked_view(view, &checked) < 0)
        return -1;
    upper = checked.entry_count;
    while(lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;
        const uint8_t *encoded = (const uint8_t *)checked.entries +
            middle * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES;

        if(read_le16(encoded) < identifier)
            lower = middle + 1u;
        else
            upper = middle;
    }
    if(lower >= checked.entry_count) {
        errno = ENOENT;
        return -1;
    }
    entry_at(&checked, lower, image);
    if(image->identifier != identifier) {
        memset(image, 0, sizeof(*image));
        errno = ENOENT;
        return -1;
    }
    return 0;
}
