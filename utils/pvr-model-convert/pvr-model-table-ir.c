/* KallistiOS ##version##

   Host-side compact model-table serializer.
   Copyright (C) 2026 Joseph Black
*/

#include "pvr-scene-ir.h"

#include <dc/pvr_chunk_model_table.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static void store_le16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void store_float(uint8_t *bytes, float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    store_le32(bytes, word);
}

static int store_ordinal(uint8_t *bytes, size_t ordinal) {
    if(ordinal != PVR_CHUNK_MODEL_SECTION_NONE && ordinal > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    store_le32(bytes, (uint32_t)ordinal);
    return 0;
}

int pvr_scene_ir_serialize_model_table(
    const pvr_chunk_model_table_record_t *records, size_t model_count,
    uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_model_table_view_t checked;
    uint8_t *bytes;
    size_t payload_bytes;
    size_t file_bytes;
    size_t index;

    if(bytes_out)
        *bytes_out = NULL;
    if(size_out)
        *size_out = 0;
    if(!records || !model_count || !bytes_out || !size_out ||
       model_count >
           (UINT32_MAX - PVR_CHUNK_MODEL_TABLE_HEADER_BYTES) /
               PVR_CHUNK_MODEL_TABLE_RECORD_BYTES) {
        errno = EINVAL;
        return -1;
    }
    payload_bytes = model_count * PVR_CHUNK_MODEL_TABLE_RECORD_BYTES;
    file_bytes = PVR_CHUNK_MODEL_TABLE_HEADER_BYTES + payload_bytes;
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }

    for(index = 0; index < model_count; ++index) {
        const pvr_chunk_model_table_record_t *record = &records[index];
        uint8_t *destination = bytes +
            PVR_CHUNK_MODEL_TABLE_HEADER_BYTES +
            index * PVR_CHUNK_MODEL_TABLE_RECORD_BYTES;

        if(record->vertex_ordinal == PVR_CHUNK_MODEL_SECTION_NONE ||
           record->polygon_ordinal == PVR_CHUNK_MODEL_SECTION_NONE ||
           !isfinite(record->center[0]) ||
           !isfinite(record->center[1]) ||
           !isfinite(record->center[2]) ||
           !isfinite(record->radius) || record->radius < 0.0f) {
            errno = EINVAL;
            goto fail;
        }
        if(store_ordinal(destination, record->vertex_ordinal) < 0 ||
           store_ordinal(destination + 4, record->polygon_ordinal) < 0 ||
           store_ordinal(destination + 8, record->resource_ordinal) < 0 ||
           store_ordinal(destination + 12, record->volume_ordinal) < 0 ||
           store_ordinal(destination + 16, record->skin4_ordinal) < 0 ||
           store_ordinal(destination + 20,
                         record->skin_general_ordinal) < 0 ||
           store_ordinal(destination + 24, record->skeleton_ordinal) < 0 ||
           store_ordinal(destination + 28, record->morph_ordinal) < 0 ||
           store_ordinal(destination + 32,
                         record->cooked_cache_ordinal) < 0)
            goto fail;
        store_float(destination + 40, record->center[0]);
        store_float(destination + 44, record->center[1]);
        store_float(destination + 48, record->center[2]);
        store_float(destination + 52, record->radius);
    }

    store_le32(bytes, PVR_CHUNK_MODEL_TABLE_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_MODEL_TABLE_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_MODEL_TABLE_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)model_count);
    store_le16(bytes + 16, PVR_CHUNK_MODEL_TABLE_RECORD_BYTES);
    store_le32(bytes + 20, crc32_bytes(
        bytes + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES, payload_bytes));
    store_le32(bytes + 28, crc32_bytes(bytes, 28));
    if(pvr_chunk_model_table_open(bytes, file_bytes, &checked) < 0)
        goto fail;

    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail: {
        int saved_errno = errno ? errno : EIO;

        free(bytes);
        errno = saved_errno;
        return -1;
    }
}
