/* KallistiOS ##version##

   dc/pvr/pvr_chunk_model_table.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model_table.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_PAYLOAD_CRC_OFFSET = 20,
    HEADER_RESERVED_OFFSET = 24,
    HEADER_CRC_OFFSET = 28,
    HEADER_CRC_BYTES = 28,
    RECORD_VERTEX_OFFSET = 0,
    RECORD_POLYGON_OFFSET = 4,
    RECORD_RESOURCE_OFFSET = 8,
    RECORD_VOLUME_OFFSET = 12,
    RECORD_SKIN4_OFFSET = 16,
    RECORD_SKIN_GENERAL_OFFSET = 20,
    RECORD_SKELETON_OFFSET = 24,
    RECORD_MORPH_OFFSET = 28,
    RECORD_COOKED_CACHE_OFFSET = 32,
    RECORD_FLAGS_OFFSET = 36,
    RECORD_CENTER_OFFSET = 40,
    RECORD_RADIUS_OFFSET = 52,
    RECORD_RESERVED_OFFSET = 56,
    RECORD_RESERVED_BYTES = 8
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

static int bytes_are_zero(const uint8_t *bytes, size_t size) {
    size_t index;

    for(index = 0; index < size; ++index) {
        if(bytes[index])
            return 0;
    }
    return 1;
}

static void decode_record(const uint8_t *bytes,
                          pvr_chunk_model_table_record_t *record) {
    record->vertex_ordinal = read_le32(bytes + RECORD_VERTEX_OFFSET);
    record->polygon_ordinal = read_le32(bytes + RECORD_POLYGON_OFFSET);
    record->resource_ordinal = read_le32(bytes + RECORD_RESOURCE_OFFSET);
    record->volume_ordinal = read_le32(bytes + RECORD_VOLUME_OFFSET);
    record->skin4_ordinal = read_le32(bytes + RECORD_SKIN4_OFFSET);
    record->skin_general_ordinal =
        read_le32(bytes + RECORD_SKIN_GENERAL_OFFSET);
    record->skeleton_ordinal = read_le32(bytes + RECORD_SKELETON_OFFSET);
    record->morph_ordinal = read_le32(bytes + RECORD_MORPH_OFFSET);
    record->cooked_cache_ordinal =
        read_le32(bytes + RECORD_COOKED_CACHE_OFFSET);
    record->center[0] = read_float(bytes + RECORD_CENTER_OFFSET);
    record->center[1] = read_float(bytes + RECORD_CENTER_OFFSET + 4);
    record->center[2] = read_float(bytes + RECORD_CENTER_OFFSET + 8);
    record->radius = read_float(bytes + RECORD_RADIUS_OFFSET);
}

static int record_is_valid(const uint8_t *bytes, size_t index,
                           uint16_t version) {
    pvr_chunk_model_table_record_t record;

    decode_record(bytes, &record);
    return record.vertex_ordinal != PVR_CHUNK_MODEL_SECTION_NONE &&
           record.polygon_ordinal != PVR_CHUNK_MODEL_SECTION_NONE &&
           (version != PVR_CHUNK_MODEL_TABLE_VERSION_1 ||
            (record.vertex_ordinal == index &&
             record.polygon_ordinal == index)) &&
           !read_le32(bytes + RECORD_FLAGS_OFFSET) &&
           bytes_are_zero(bytes + RECORD_RESERVED_OFFSET,
                          RECORD_RESERVED_BYTES) &&
           isfinite(record.center[0]) && isfinite(record.center[1]) &&
           isfinite(record.center[2]) && isfinite(record.radius) &&
           record.radius >= 0.0f;
}

int pvr_chunk_model_table_open(
    const void *data, size_t size, pvr_chunk_model_table_view_t *view) {
    const uint8_t *bytes = data;
    pvr_chunk_model_table_view_t parsed;
    uint32_t file_bytes;
    uint32_t model_count;
    uint16_t record_stride;
    uint16_t version;
    size_t payload_bytes;
    size_t index;

    if(view)
        memset(view, 0, sizeof(*view));
    if(!data || !view) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CHUNK_MODEL_TABLE_HEADER_BYTES ||
       read_le32(bytes) != PVR_CHUNK_MODEL_TABLE_MAGIC ||
       read_le16(bytes + 6) != PVR_CHUNK_MODEL_TABLE_HEADER_BYTES ||
       read_le16(bytes + 18) ||
       read_le32(bytes + HEADER_RESERVED_OFFSET) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }

    version = read_le16(bytes + 4);
    if(version != PVR_CHUNK_MODEL_TABLE_VERSION_1 &&
       version != PVR_CHUNK_MODEL_TABLE_VERSION) {
        errno = EILSEQ;
        return -1;
    }

    file_bytes = read_le32(bytes + 8);
    model_count = read_le32(bytes + 12);
    record_stride = read_le16(bytes + 16);
    if(file_bytes != size || !model_count ||
       record_stride != PVR_CHUNK_MODEL_TABLE_RECORD_BYTES ||
       model_count >
           (SIZE_MAX - PVR_CHUNK_MODEL_TABLE_HEADER_BYTES) / record_stride) {
        errno = EILSEQ;
        return -1;
    }
    payload_bytes = (size_t)model_count * record_stride;
    if(payload_bytes != file_bytes - PVR_CHUNK_MODEL_TABLE_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES, payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }
    for(index = 0; index < model_count; ++index) {
        if(!record_is_valid(
               bytes + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES +
                   index * record_stride,
               index, version)) {
            errno = EILSEQ;
            return -1;
        }
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = file_bytes;
    parsed.records = bytes + PVR_CHUNK_MODEL_TABLE_HEADER_BYTES;
    parsed.model_count = model_count;
    parsed.record_stride = record_stride;
    parsed.version = version;
    *view = parsed;
    return 0;
}

int pvr_chunk_model_table_record_get(
    const pvr_chunk_model_table_view_t *view, size_t model_ordinal,
    pvr_chunk_model_table_record_t *record) {
    pvr_chunk_model_table_view_t checked;

    if(record)
        memset(record, 0, sizeof(*record));
    if(!view || !record || !view->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_table_open(view->data, view->size, &checked) < 0)
        return -1;
    if(model_ordinal >= checked.model_count) {
        errno = ENOENT;
        return -1;
    }
    decode_record(
        (const uint8_t *)checked.records +
            model_ordinal * checked.record_stride,
        record);
    return 0;
}

static int optional_section_exists(const pvr_chunk_asset_view_t *asset,
                                   size_t ordinal, uint32_t type) {
    pvr_chunk_asset_section_t section;

    if(ordinal == PVR_CHUNK_MODEL_SECTION_NONE)
        return 0;
    if(pvr_chunk_asset_section_find(asset, type, ordinal, &section) < 0) {
        if(errno == ENOENT)
            errno = EILSEQ;
        return -1;
    }
    return 0;
}

int pvr_chunk_model_table_validate_asset(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset) {
    pvr_chunk_model_table_view_t checked_table;
    pvr_chunk_asset_view_t checked_asset;
    size_t index;

    if(!view || !asset || !view->data || !asset->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_table_open(
           view->data, view->size, &checked_table) < 0 ||
       pvr_chunk_asset_open(
           asset->data, asset->size, &checked_asset) < 0)
        return -1;
    if(checked_table.version == PVR_CHUNK_MODEL_TABLE_VERSION_1 &&
       checked_table.model_count != checked_asset.model_count) {
        errno = EILSEQ;
        return -1;
    }
    for(index = 0; index < checked_table.model_count; ++index) {
        pvr_chunk_model_table_record_t record;

        decode_record(
            (const uint8_t *)checked_table.records +
                index * checked_table.record_stride,
            &record);
        if(optional_section_exists(
               &checked_asset, record.vertex_ordinal,
               PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM) < 0 ||
           optional_section_exists(
               &checked_asset, record.polygon_ordinal,
               PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM) < 0 ||
           optional_section_exists(
               &checked_asset, record.resource_ordinal,
               PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE) < 0 ||
           optional_section_exists(
               &checked_asset, record.volume_ordinal,
               PVR_CHUNK_ASSET_SECTION_VOLUME_DATA) < 0 ||
           optional_section_exists(
               &checked_asset, record.skin4_ordinal,
               PVR_CHUNK_ASSET_SECTION_SKIN4) < 0 ||
           optional_section_exists(
               &checked_asset, record.skin_general_ordinal,
               PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL) < 0 ||
           optional_section_exists(
               &checked_asset, record.skeleton_ordinal,
               PVR_CHUNK_ASSET_SECTION_SKELETON) < 0 ||
           optional_section_exists(
               &checked_asset, record.morph_ordinal,
               PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS) < 0 ||
           optional_section_exists(
               &checked_asset, record.cooked_cache_ordinal,
               PVR_CHUNK_ASSET_SECTION_COOKED_CACHE) < 0)
            return -1;
    }
    return 0;
}

int pvr_chunk_model_table_workspace_query(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset, size_t model_ordinal,
    pvr_chunk_asset_workspace_requirements_t *requirements) {
    pvr_chunk_model_table_record_t record;

    if(requirements)
        memset(requirements, 0, sizeof(*requirements));
    if(!view || !asset || !requirements || !view->data || !asset->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_table_validate_asset(view, asset) < 0 ||
       pvr_chunk_model_table_record_get(
           view, model_ordinal, &record) < 0)
        return -1;
    return pvr_chunk_asset_pair_workspace_query(
        asset, record.vertex_ordinal, record.polygon_ordinal, requirements);
}

int pvr_chunk_model_table_load(
    const pvr_chunk_model_table_view_t *view,
    const pvr_chunk_asset_view_t *asset, size_t model_ordinal,
    pvr_chunk_asset_decoder_t decoder, void *decoder_data,
    void *workspace, size_t workspace_bytes,
    pvr_chunk_model_view_t *model_view) {
    pvr_chunk_model_table_record_t record;
    pvr_chunk_model_view_t loaded;

    if(model_view)
        memset(model_view, 0, sizeof(*model_view));
    if(!view || !asset || !model_view || !view->data || !asset->data) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_chunk_model_table_validate_asset(view, asset) < 0 ||
       pvr_chunk_model_table_record_get(
           view, model_ordinal, &record) < 0 ||
       pvr_chunk_asset_pair_load(
           asset, record.vertex_ordinal, record.polygon_ordinal,
           decoder, decoder_data,
           workspace, workspace_bytes, &loaded) < 0)
        return -1;

    memcpy(loaded.model.center, record.center,
           sizeof(loaded.model.center));
    loaded.model.radius = record.radius;
    *model_view = loaded;
    return 0;
}
