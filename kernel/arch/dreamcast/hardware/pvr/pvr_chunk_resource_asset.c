/* KallistiOS ##version##

   dc/pvr/pvr_chunk_resource_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_resource_asset.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 24,
    HEADER_RESERVED0_OFFSET = 28,
    HEADER_RESERVED1_OFFSET = 32,
    HEADER_RESERVED2_OFFSET = 36,
    HEADER_RESERVED3_OFFSET = 40,
    HEADER_CRC_OFFSET = 44,
    HEADER_CRC_BYTES = 44
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

static void decode_entry(const uint8_t *bytes,
                         pvr_chunk_resource_entry_t *entry) {
    entry->identifier = read_le16(bytes);
    entry->usage = read_le16(bytes + 2);
}

static void entry_at(const pvr_chunk_resource_section_view_t *view,
                     size_t index, pvr_chunk_resource_entry_t *entry) {
    decode_entry((const uint8_t *)view->entries +
                     index * PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES,
                 entry);
}

static int find_checked(const pvr_chunk_resource_section_view_t *view,
                        uint16_t identifier,
                        pvr_chunk_resource_entry_t *entry) {
    size_t lower = 0;
    size_t upper = view->entry_count;

    while(lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;
        pvr_chunk_resource_entry_t candidate;

        entry_at(view, middle, &candidate);
        if(candidate.identifier < identifier)
            lower = middle + 1u;
        else
            upper = middle;
    }
    if(lower == view->entry_count) {
        errno = ENOENT;
        return -1;
    }
    entry_at(view, lower, entry);
    if(entry->identifier != identifier) {
        memset(entry, 0, sizeof(*entry));
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static int checked_view(const pvr_chunk_resource_section_view_t *view,
                        pvr_chunk_resource_section_view_t *checked) {
    if(!view || !checked || !view->data) {
        errno = EINVAL;
        return -1;
    }
    return pvr_chunk_resource_section_open(view->data, view->size, checked);
}

int pvr_chunk_resource_section_open(
    const void *data, size_t size,
    pvr_chunk_resource_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_resource_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t entry_count;
    uint32_t entries_bytes;
    uint16_t previous = 0;
    size_t index;

    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_RESOURCE_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_RESOURCE_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES ||
       read_le16(bytes + 16) != PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES ||
       read_le16(bytes + 18) || read_le32(bytes + HEADER_RESERVED0_OFFSET) ||
       read_le32(bytes + HEADER_RESERVED1_OFFSET) ||
       read_le32(bytes + HEADER_RESERVED2_OFFSET) ||
       read_le32(bytes + HEADER_RESERVED3_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    entry_count = read_le32(bytes + 12);
    entries_bytes = read_le32(bytes + 20);
    if(file_bytes != size || !entry_count ||
       entry_count > UINT32_MAX / PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES ||
       entries_bytes != entry_count *
                            PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES ||
       entries_bytes != file_bytes -
                            PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES,
           entries_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.entries = bytes + PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES;
    parsed.entry_count = entry_count;
    parsed.version = PVR_CHUNK_RESOURCE_SECTION_VERSION;
    for(index = 0; index < parsed.entry_count; ++index) {
        const uint8_t *encoded = (const uint8_t *)parsed.entries +
            index * PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES;
        pvr_chunk_resource_entry_t entry;

        decode_entry(encoded, &entry);
        if(entry.identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX ||
           !entry.usage ||
           (entry.usage & ~(PVR_CHUNK_RESOURCE_PRIMARY |
                            PVR_CHUNK_RESOURCE_SECONDARY)) ||
           read_le32(encoded + 4) ||
           (index && entry.identifier <= previous)) {
            errno = EILSEQ;
            return -1;
        }
        previous = entry.identifier;
    }
    *view = parsed;
    return 0;
}

int pvr_chunk_resource_section_entry_get(
    const pvr_chunk_resource_section_view_t *view, size_t index,
    pvr_chunk_resource_entry_t *entry) {
    pvr_chunk_resource_section_view_t checked;

    if(entry)
        memset(entry, 0, sizeof(*entry));
    if(!entry) {
        errno = EINVAL;
        return -1;
    }
    if(checked_view(view, &checked) < 0)
        return -1;
    if(index >= checked.entry_count) {
        errno = ENOENT;
        return -1;
    }
    entry_at(&checked, index, entry);
    return 0;
}

int pvr_chunk_resource_section_find(
    const pvr_chunk_resource_section_view_t *view, uint16_t identifier,
    pvr_chunk_resource_entry_t *entry) {
    pvr_chunk_resource_section_view_t checked;
    if(entry)
        memset(entry, 0, sizeof(*entry));
    if(!entry) {
        errno = EINVAL;
        return -1;
    }
    if(identifier > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX) {
        errno = ERANGE;
        return -1;
    }
    if(checked_view(view, &checked) < 0)
        return -1;
    return find_checked(&checked, identifier, entry);
}

static int model_usage(const pvr_chunk_model_view_t *model,
                       uint16_t identifier, uint16_t *usage) {
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    *usage = 0;
    if(pvr_chunk_polygon_iterator_init(&iterator,
                                       model->model.polygon_words,
                                       model->model.polygon_word_count) < 0)
        return -1;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        uint16_t encoded;

        if(record.record_class != PVR_CHUNK_RECORD_TEXTURE)
            continue;
        if(record.payload_word_count != 1u || !record.payload) {
            errno = EILSEQ;
            return -1;
        }
        encoded = *(const uint16_t *)record.payload;
        if((encoded & PVR_CHUNK_TEXTURE_IDENTIFIER_MAX) != identifier)
            continue;
        *usage |= record.type == PVR_CHUNK_TEXTURE_TWO_VOLUME ?
                  PVR_CHUNK_RESOURCE_SECONDARY :
                  PVR_CHUNK_RESOURCE_PRIMARY;
    }
    return rv < 0 ? -1 : 0;
}

int pvr_chunk_resource_section_validate_model(
    const pvr_chunk_resource_section_view_t *view,
    const pvr_chunk_model_view_t *model) {
    pvr_chunk_resource_section_view_t checked;
    pvr_chunk_model_view_t admitted;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t index;
    int rv;

    if(!model || checked_view(view, &checked) < 0) {
        if(!model)
            errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&model->model, &admitted) < 0 ||
       pvr_chunk_polygon_iterator_init(&iterator,
           admitted.model.polygon_words,
           admitted.model.polygon_word_count) < 0)
        return -1;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        pvr_chunk_resource_entry_t entry;
        uint16_t identifier;
        uint16_t usage;

        if(record.record_class != PVR_CHUNK_RECORD_TEXTURE)
            continue;
        if(record.payload_word_count != 1u || !record.payload) {
            errno = EILSEQ;
            return -1;
        }
        identifier = *(const uint16_t *)record.payload &
                     PVR_CHUNK_TEXTURE_IDENTIFIER_MAX;
        usage = record.type == PVR_CHUNK_TEXTURE_TWO_VOLUME ?
                PVR_CHUNK_RESOURCE_SECONDARY :
                PVR_CHUNK_RESOURCE_PRIMARY;
        if(find_checked(&checked, identifier, &entry) < 0 ||
           !(entry.usage & usage)) {
            if(errno == ENOENT || !(entry.usage & usage))
                errno = EILSEQ;
            return -1;
        }
    }
    if(rv < 0)
        return -1;
    for(index = 0; index < checked.entry_count; ++index) {
        pvr_chunk_resource_entry_t entry;
        uint16_t usage;

        entry_at(&checked, index, &entry);
        if(model_usage(&admitted, entry.identifier, &usage) < 0)
            return -1;
        if(usage != entry.usage) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}
