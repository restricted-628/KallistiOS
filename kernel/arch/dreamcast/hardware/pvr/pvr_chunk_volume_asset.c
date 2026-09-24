/* KallistiOS ##version##

   dc/pvr/pvr_chunk_volume_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_volume_asset.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 32,
    HEADER_RESERVED0_OFFSET = 36,
    HEADER_RESERVED1_OFFSET = 40,
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

static void decode_span(const uint8_t *record,
                        pvr_chunk_volume_section_record_t *span) {
    span->first_word = read_le32(record);
    span->word_count = read_le32(record + 4);
}

/* The section deliberately retains complete volume records. This adapter is
   the only bridge needed to reuse the compact topology validator/iterator. */
static int record_at(const pvr_chunk_volume_section_view_t *view,
                     size_t index, pvr_chunk_record_t *record) {
    pvr_chunk_volume_section_record_t span;
    pvr_chunk_iterator_t iterator;
    int rv;

    if(!view || !record || index >= view->record_count) {
        errno = !view || !record ? EINVAL : ENOENT;
        return -1;
    }
    decode_span((const uint8_t *)view->records +
                    index * PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES,
                &span);
    if(!span.word_count || span.first_word > view->word_count ||
       span.word_count > view->word_count - span.first_word ||
       pvr_chunk_polygon_iterator_init(&iterator,
                                       view->words + span.first_word,
                                       span.word_count) < 0)
        return -1;
    rv = pvr_chunk_iterator_next(&iterator, record);
    if(rv != 1 || record->record_class != PVR_CHUNK_RECORD_VOLUME ||
       record->word_count != span.word_count) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_volume_section_open(
    const void *data, size_t size, pvr_chunk_volume_section_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_volume_section_view_t parsed;
    uint32_t file_bytes;
    uint32_t record_count;
    uint32_t word_count;
    uint32_t record_bytes;
    uint32_t stream_bytes;
    uint64_t encoded_payload_bytes;
    size_t next_word = 0;
    size_t record_index;

    if(!data || !view || ((uintptr_t)data & (_Alignof(uint16_t) - 1u))) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_VOLUME_SECTION_MAGIC ||
       read_le16(bytes + 4) != PVR_CHUNK_VOLUME_SECTION_VERSION ||
       read_le16(bytes + 6) != PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES ||
       read_le16(bytes + 20) != PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES ||
       read_le16(bytes + 22) != sizeof(uint16_t) ||
       read_le32(bytes + HEADER_RESERVED0_OFFSET) ||
       read_le32(bytes + HEADER_RESERVED1_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    record_count = read_le32(bytes + 12);
    word_count = read_le32(bytes + 16);
    record_bytes = read_le32(bytes + 24);
    stream_bytes = read_le32(bytes + 28);
    if(file_bytes != size || !record_count || !word_count ||
       record_count > UINT32_MAX / PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES ||
       word_count > UINT32_MAX / sizeof(uint16_t) ||
       record_bytes != record_count *
                           PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES ||
       stream_bytes != word_count * sizeof(uint16_t)) {
        errno = EILSEQ;
        return -1;
    }
    encoded_payload_bytes = (uint64_t)record_bytes + stream_bytes;
    if(encoded_payload_bytes !=
       (uint64_t)file_bytes - PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES,
           (size_t)encoded_payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.records = bytes + PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES;
    parsed.record_count = record_count;
    parsed.words = (const uint16_t *)(bytes +
        PVR_CHUNK_VOLUME_SECTION_HEADER_BYTES + record_bytes);
    parsed.word_count = word_count;
    parsed.version = PVR_CHUNK_VOLUME_SECTION_VERSION;

    for(record_index = 0; record_index < parsed.record_count;
        ++record_index) {
        pvr_chunk_volume_section_record_t span;
        pvr_chunk_record_t record;
        size_t triangles;

        decode_span((const uint8_t *)parsed.records + record_index *
                        PVR_CHUNK_VOLUME_SECTION_RECORD_BYTES,
                    &span);
        if(!span.word_count || span.first_word != next_word ||
           span.word_count > parsed.word_count - next_word) {
            errno = EILSEQ;
            return -1;
        }
        if(record_at(&parsed, record_index, &record) < 0 ||
           pvr_chunk_volume_triangle_count(&record, &triangles) < 0)
            return -1;
        if(triangles > SIZE_MAX - parsed.triangle_count) {
            errno = EOVERFLOW;
            return -1;
        }
        parsed.triangle_count += triangles;
        next_word += span.word_count;
    }
    if(next_word != parsed.word_count) {
        errno = EILSEQ;
        return -1;
    }

    *view = parsed;
    return 0;
}

int pvr_chunk_volume_section_record_get(
    const pvr_chunk_volume_section_view_t *view, size_t index,
    pvr_chunk_record_t *record) {
    pvr_chunk_volume_section_view_t checked;

    if(record)
        memset(record, 0, sizeof(*record));
    if(!view || !record || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_volume_section_open(view->data, view->size, &checked) < 0)
        return -1;
    return record_at(&checked, index, record);
}

int pvr_chunk_volume_section_iterator_init(
    pvr_chunk_volume_section_iterator_t *iterator,
    const pvr_chunk_volume_section_view_t *view) {
    pvr_chunk_volume_section_iterator_t prepared;

    if(iterator)
        memset(iterator, 0, sizeof(*iterator));
    if(!iterator || !view || !view->data) {
        errno = EINVAL;
        return -1;
    }
    memset(&prepared, 0, sizeof(prepared));
    if(pvr_chunk_volume_section_open(view->data, view->size,
                                     &prepared.view) < 0)
        return -1;
    *iterator = prepared;
    return 0;
}

int pvr_chunk_volume_section_iterator_next(
    pvr_chunk_volume_section_iterator_t *iterator,
    pvr_chunk_volume_triangle_t *triangle) {
    if(triangle)
        memset(triangle, 0, sizeof(*triangle));
    if(!iterator || !triangle || !iterator->view.data) {
        errno = EINVAL;
        return -1;
    }

    for(;;) {
        int rv;

        if(iterator->active) {
            rv = pvr_chunk_volume_iterator_next(&iterator->volume,
                                                 triangle);
            if(rv != 0)
                return rv;
            iterator->active = 0;
            ++iterator->record_index;
        }
        if(iterator->record_index >= iterator->view.record_count)
            return 0;
        {
            pvr_chunk_record_t record;

            if(record_at(&iterator->view, iterator->record_index,
                         &record) < 0 ||
               pvr_chunk_volume_iterator_init(&iterator->volume,
                                              &record) < 0)
                return -1;
        }
        iterator->active = 1;
    }
}

int pvr_chunk_volume_section_validate_model(
    const pvr_chunk_volume_section_view_t *view,
    const pvr_chunk_model_view_t *model) {
    pvr_chunk_volume_section_iterator_t iterator;
    pvr_chunk_volume_triangle_t triangle;
    pvr_chunk_model_view_t admitted;
    int rv;

    if(!view || !model) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_open(&model->model, &admitted) < 0 ||
       pvr_chunk_volume_section_iterator_init(&iterator, view) < 0)
        return -1;

    while((rv = pvr_chunk_volume_section_iterator_next(
               &iterator, &triangle)) > 0) {
        size_t corner;

        for(corner = 0; corner < 3; ++corner) {
            pvr_chunk_vertex_attributes_t attributes;

            if(pvr_chunk_model_vertex_attributes_get(
                   &admitted, triangle.index[corner], &attributes) < 0) {
                if(errno == ENOENT)
                    errno = EILSEQ;
                return -1;
            }
        }
    }
    return rv;
}
