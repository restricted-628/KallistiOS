/* KallistiOS ##version##

   dc/pvr/pvr_cell_asset.c
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_cell_asset.h>

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* PCA1 keeps complete cells, stream spans, and partial cell keys in three
   contiguous fixed-record arrays. Stream records use global key ordinals, so
   admission can prove every key has exactly one owner before pointers are
   materialized into the existing runtime. */
enum {
    HEADER_PAYLOAD_CRC_OFFSET = 44,
    HEADER_RESERVED_OFFSET = 48,
    HEADER_CRC_OFFSET = 60,
    HEADER_CRC_BYTES = 60,
    STATE_ATLAS_OFFSET = 0,
    STATE_POSITION_OFFSET = 4,
    STATE_ROTATION_OFFSET = 16,
    STATE_SCALE_OFFSET = 20,
    STATE_PRIORITY_OFFSET = 28,
    STATE_FLAGS_OFFSET = 32,
    STATE_MATERIAL_OFFSET = 36,
    STATE_DIFFUSE_OFFSET = 40,
    STATE_SPECULAR_OFFSET = 56,
    STREAM_FIRST_KEY_OFFSET = 0,
    STREAM_KEY_COUNT_OFFSET = 4,
    STREAM_TIME_OFFSET = 8,
    STREAM_TIME_MAX_OFFSET = 12,
    STREAM_REPEAT_OFFSET = 16,
    KEY_TIME_OFFSET = 0,
    KEY_SLOT_OFFSET = 4,
    KEY_FIELDS_OFFSET = 8,
    KEY_RESERVED_OFFSET = 12,
    KEY_STATE_OFFSET = 16
};

#define PVR_CELL_FLAGS_ALL \
    (PVR_CELL_FLIP_U | PVR_CELL_FLIP_V | PVR_CELL_HIDDEN)

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

static int range_valid(const void *data, size_t count, size_t stride) {
    uintptr_t address = (uintptr_t)data;
    size_t bytes;

    if(count > SIZE_MAX / stride)
        return 0;
    bytes = count * stride;
    return bytes <= UINTPTR_MAX - address;
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

static int bytes_zero(const uint8_t *bytes, size_t size) {
    size_t index;

    for(index = 0; index < size; ++index) {
        if(bytes[index])
            return 0;
    }
    return 1;
}

static int state_fields_valid(const pvr_cell_state_t *state,
                              uint32_t fields) {
    if((fields & PVR_CELL_KEY_OFFSET) &&
       (!isfinite(state->offset.x) || !isfinite(state->offset.y) ||
        !isfinite(state->offset.z)))
        return 0;
    if((fields & PVR_CELL_KEY_ROTATION) && !isfinite(state->rotation))
        return 0;
    if((fields & PVR_CELL_KEY_SCALE) &&
       (!isfinite(state->scale_x) || !isfinite(state->scale_y) ||
        state->scale_x <= 0.0f || state->scale_y <= 0.0f))
        return 0;
    if((fields & PVR_CELL_KEY_FLAGS) &&
       (state->flags & ~PVR_CELL_FLAGS_ALL))
        return 0;
    return 1;
}

static int state_valid(const pvr_cell_state_t *state) {
    return state && state_fields_valid(state, PVR_CELL_KEY_ALL);
}

static void encode_state(uint8_t *record, const pvr_cell_state_t *state,
                         uint32_t fields) {
    size_t index;

    if(fields & PVR_CELL_KEY_ATLAS_CELL)
        write_le32(record + STATE_ATLAS_OFFSET,
                   (uint32_t)state->atlas_cell_index);
    if(fields & PVR_CELL_KEY_OFFSET) {
        write_float(record + STATE_POSITION_OFFSET, state->offset.x);
        write_float(record + STATE_POSITION_OFFSET + 4, state->offset.y);
        write_float(record + STATE_POSITION_OFFSET + 8, state->offset.z);
    }
    if(fields & PVR_CELL_KEY_ROTATION)
        write_float(record + STATE_ROTATION_OFFSET, state->rotation);
    if(fields & PVR_CELL_KEY_SCALE) {
        write_float(record + STATE_SCALE_OFFSET, state->scale_x);
        write_float(record + STATE_SCALE_OFFSET + 4, state->scale_y);
    }
    if(fields & PVR_CELL_KEY_PRIORITY)
        write_le32(record + STATE_PRIORITY_OFFSET,
                   (uint32_t)state->priority);
    if(fields & PVR_CELL_KEY_FLAGS)
        write_le32(record + STATE_FLAGS_OFFSET, state->flags);
    if(fields & PVR_CELL_KEY_MATERIAL)
        write_le32(record + STATE_MATERIAL_OFFSET, state->material_id);
    if(fields & PVR_CELL_KEY_DIFFUSE) {
        for(index = 0; index < 4u; ++index)
            write_le32(record + STATE_DIFFUSE_OFFSET + index * 4u,
                       state->argb[index]);
    }
    if(fields & PVR_CELL_KEY_SPECULAR) {
        for(index = 0; index < 4u; ++index)
            write_le32(record + STATE_SPECULAR_OFFSET + index * 4u,
                       state->oargb[index]);
    }
}

static void decode_state(const uint8_t *record, pvr_cell_state_t *state) {
    size_t index;

    memset(state, 0, sizeof(*state));
    state->atlas_cell_index = read_le32(record + STATE_ATLAS_OFFSET);
    state->offset.x = read_float(record + STATE_POSITION_OFFSET);
    state->offset.y = read_float(record + STATE_POSITION_OFFSET + 4);
    state->offset.z = read_float(record + STATE_POSITION_OFFSET + 8);
    state->offset.w = 1.0f;
    state->rotation = read_float(record + STATE_ROTATION_OFFSET);
    state->scale_x = read_float(record + STATE_SCALE_OFFSET);
    state->scale_y = read_float(record + STATE_SCALE_OFFSET + 4);
    state->priority = (int32_t)read_le32(record + STATE_PRIORITY_OFFSET);
    state->flags = read_le32(record + STATE_FLAGS_OFFSET);
    state->material_id = read_le32(record + STATE_MATERIAL_OFFSET);
    for(index = 0; index < 4u; ++index) {
        state->argb[index] = read_le32(
            record + STATE_DIFFUSE_OFFSET + index * 4u);
        state->oargb[index] = read_le32(
            record + STATE_SPECULAR_OFFSET + index * 4u);
    }
}

static int unused_key_fields_zero(const uint8_t *state, uint32_t fields) {
    if(!(fields & PVR_CELL_KEY_ATLAS_CELL) &&
       !bytes_zero(state + STATE_ATLAS_OFFSET, 4u))
        return 0;
    if(!(fields & PVR_CELL_KEY_OFFSET) &&
       !bytes_zero(state + STATE_POSITION_OFFSET, 12u))
        return 0;
    if(!(fields & PVR_CELL_KEY_ROTATION) &&
       !bytes_zero(state + STATE_ROTATION_OFFSET, 4u))
        return 0;
    if(!(fields & PVR_CELL_KEY_SCALE) &&
       !bytes_zero(state + STATE_SCALE_OFFSET, 8u))
        return 0;
    if(!(fields & PVR_CELL_KEY_PRIORITY) &&
       !bytes_zero(state + STATE_PRIORITY_OFFSET, 4u))
        return 0;
    if(!(fields & PVR_CELL_KEY_FLAGS) &&
       !bytes_zero(state + STATE_FLAGS_OFFSET, 4u))
        return 0;
    if(!(fields & PVR_CELL_KEY_MATERIAL) &&
       !bytes_zero(state + STATE_MATERIAL_OFFSET, 4u))
        return 0;
    if(!(fields & PVR_CELL_KEY_DIFFUSE) &&
       !bytes_zero(state + STATE_DIFFUSE_OFFSET, 16u))
        return 0;
    if(!(fields & PVR_CELL_KEY_SPECULAR) &&
       !bytes_zero(state + STATE_SPECULAR_OFFSET, 16u))
        return 0;
    return 1;
}

static void decode_stream(const uint8_t *record,
                          pvr_cell_asset_stream_t *stream) {
    stream->first_key = read_le32(record + STREAM_FIRST_KEY_OFFSET);
    stream->key_count = read_le32(record + STREAM_KEY_COUNT_OFFSET);
    stream->time_offset = read_float(record + STREAM_TIME_OFFSET);
    stream->time_max = read_float(record + STREAM_TIME_MAX_OFFSET);
    stream->repeat = read_le32(record + STREAM_REPEAT_OFFSET);
}

static int decode_key(const uint8_t *record, pvr_cell_key_t *key) {
    uint32_t fields = read_le32(record + KEY_FIELDS_OFFSET);

    memset(key, 0, sizeof(*key));
    key->time = read_float(record + KEY_TIME_OFFSET);
    key->slot_index = read_le32(record + KEY_SLOT_OFFSET);
    key->fields = fields;
    decode_state(record + KEY_STATE_OFFSET, &key->value);
    if(!isfinite(key->time) || !fields ||
       (fields & ~PVR_CELL_KEY_ALL) ||
       read_le32(record + KEY_RESERVED_OFFSET) ||
       !unused_key_fields_zero(record + KEY_STATE_OFFSET, fields) ||
       !state_fields_valid(&key->value, fields)) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int source_measure(const pvr_cell_state_t *base_cells,
                          size_t cell_count,
                          const pvr_cell_stream_t *streams,
                          size_t stream_count, size_t *key_count,
                          size_t *required_bytes) {
    uint64_t total;
    size_t keys = 0;
    size_t index;

    if(!base_cells || !cell_count || cell_count > UINT32_MAX ||
       (stream_count && !streams) || stream_count > UINT32_MAX ||
       ((uintptr_t)base_cells & (_Alignof(pvr_cell_state_t) - 1u)) ||
       (streams && ((uintptr_t)streams &
                    (_Alignof(pvr_cell_stream_t) - 1u))) ||
       !range_valid(base_cells, cell_count, sizeof(*base_cells)) ||
       !range_valid(streams, stream_count, sizeof(*streams))) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < cell_count; ++index) {
        if(!state_valid(&base_cells[index])) {
            errno = EINVAL;
            return -1;
        }
#if SIZE_MAX > UINT32_MAX
        if(base_cells[index].atlas_cell_index > UINT32_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
#endif
    }
    for(index = 0; index < stream_count; ++index) {
        pvr_cell_stream_view_t checked;
        size_t key;

        if(pvr_cell_stream_open(&streams[index], cell_count, &checked) < 0)
            return -1;
        if(streams[index].key_count > UINT32_MAX - keys) {
            errno = EOVERFLOW;
            return -1;
        }
        for(key = 0; key < streams[index].key_count; ++key) {
#if SIZE_MAX > UINT32_MAX
            if(streams[index].keys[key].slot_index > UINT32_MAX ||
               ((streams[index].keys[key].fields &
                 PVR_CELL_KEY_ATLAS_CELL) &&
                streams[index].keys[key].value.atlas_cell_index >
                    UINT32_MAX)) {
                errno = EOVERFLOW;
                return -1;
            }
#endif
        }
        keys += streams[index].key_count;
    }
    total = PVR_CELL_ASSET_HEADER_BYTES +
            (uint64_t)cell_count * PVR_CELL_ASSET_STATE_BYTES +
            (uint64_t)stream_count * PVR_CELL_ASSET_STREAM_BYTES +
            (uint64_t)keys * PVR_CELL_ASSET_KEY_BYTES;
    if(total > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    *key_count = keys;
    *required_bytes = (size_t)total;
    return 0;
}

int pvr_cell_asset_measure(const pvr_cell_state_t *base_cells,
                           size_t cell_count,
                           const pvr_cell_stream_t *streams,
                           size_t stream_count,
                           size_t *required_bytes) {
    size_t key_count;
    size_t bytes;

    size_t index;

    if(!required_bytes ||
       ((uintptr_t)required_bytes & (_Alignof(size_t) - 1u)) ||
       !range_valid(required_bytes, 1u, sizeof(*required_bytes))) {
        errno = EINVAL;
        return -1;
    }
    if(source_measure(base_cells, cell_count, streams, stream_count,
                      &key_count, &bytes) < 0)
        return -1;
    if(ranges_overlap(required_bytes, sizeof(*required_bytes), base_cells,
                      cell_count * sizeof(*base_cells)) ||
       ranges_overlap(required_bytes, sizeof(*required_bytes), streams,
                      stream_count * sizeof(*streams))) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < stream_count; ++index) {
        if(ranges_overlap(required_bytes, sizeof(*required_bytes),
                          streams[index].keys,
                          streams[index].key_count *
                              sizeof(*streams[index].keys))) {
            errno = EINVAL;
            return -1;
        }
    }
    (void)key_count;
    *required_bytes = bytes;
    return 0;
}

int pvr_cell_asset_encode(const pvr_cell_state_t *base_cells,
                          size_t cell_count,
                          const pvr_cell_stream_t *streams,
                          size_t stream_count,
                          void *output, size_t output_size,
                          size_t *written_bytes) {
    uint8_t *bytes = output;
    uint8_t *cells;
    uint8_t *stream_records;
    uint8_t *key_records;
    size_t cell_bytes;
    size_t stream_bytes;
    size_t key_count;
    size_t required;
    size_t next_key = 0;
    size_t index;

    if(written_bytes &&
       (((uintptr_t)written_bytes & (_Alignof(size_t) - 1u)) ||
        !range_valid(written_bytes, 1u, sizeof(*written_bytes)))) {
        errno = EINVAL;
        return -1;
    }
    if(source_measure(base_cells, cell_count, streams, stream_count,
                      &key_count, &required) < 0)
        return -1;
    if(!output || !range_valid(output, output_size, 1u)) {
        errno = EINVAL;
        return -1;
    }
    if(output_size < required) {
        errno = ENOSPC;
        return -1;
    }
    if(ranges_overlap(output, required, base_cells,
                      cell_count * sizeof(*base_cells)) ||
       ranges_overlap(output, required, streams,
                      stream_count * sizeof(*streams)) ||
       (written_bytes && ranges_overlap(output, required, written_bytes,
                                        sizeof(*written_bytes)))) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < stream_count; ++index) {
        if(ranges_overlap(output, required, streams[index].keys,
                          streams[index].key_count *
                              sizeof(*streams[index].keys))) {
            errno = EINVAL;
            return -1;
        }
    }

    memset(bytes, 0, required);
    cell_bytes = cell_count * PVR_CELL_ASSET_STATE_BYTES;
    stream_bytes = stream_count * PVR_CELL_ASSET_STREAM_BYTES;
    cells = bytes + PVR_CELL_ASSET_HEADER_BYTES;
    stream_records = cells + cell_bytes;
    key_records = stream_records + stream_bytes;

    for(index = 0; index < cell_count; ++index)
        encode_state(cells + index * PVR_CELL_ASSET_STATE_BYTES,
                     &base_cells[index], PVR_CELL_KEY_ALL);
    for(index = 0; index < stream_count; ++index) {
        const pvr_cell_stream_t *stream = &streams[index];
        uint8_t *record = stream_records +
                          index * PVR_CELL_ASSET_STREAM_BYTES;
        size_t key;

        write_le32(record + STREAM_FIRST_KEY_OFFSET, (uint32_t)next_key);
        write_le32(record + STREAM_KEY_COUNT_OFFSET,
                   (uint32_t)stream->key_count);
        write_float(record + STREAM_TIME_OFFSET, stream->time_offset);
        write_float(record + STREAM_TIME_MAX_OFFSET, stream->time_max);
        write_le32(record + STREAM_REPEAT_OFFSET, stream->repeat);
        for(key = 0; key < stream->key_count; ++key) {
            const pvr_cell_key_t *source = &stream->keys[key];
            uint8_t *key_record = key_records +
                (next_key + key) * PVR_CELL_ASSET_KEY_BYTES;

            write_float(key_record + KEY_TIME_OFFSET, source->time);
            write_le32(key_record + KEY_SLOT_OFFSET,
                       (uint32_t)source->slot_index);
            write_le32(key_record + KEY_FIELDS_OFFSET, source->fields);
            encode_state(key_record + KEY_STATE_OFFSET, &source->value,
                         source->fields);
        }
        next_key += stream->key_count;
    }

    write_le32(bytes, PVR_CELL_ASSET_MAGIC);
    write_le16(bytes + 4, PVR_CELL_ASSET_VERSION);
    write_le16(bytes + 6, PVR_CELL_ASSET_HEADER_BYTES);
    write_le32(bytes + 8, (uint32_t)required);
    write_le32(bytes + 12, (uint32_t)cell_count);
    write_le32(bytes + 16, (uint32_t)stream_count);
    write_le32(bytes + 20, (uint32_t)key_count);
    write_le16(bytes + 24, PVR_CELL_ASSET_STATE_BYTES);
    write_le16(bytes + 26, PVR_CELL_ASSET_STREAM_BYTES);
    write_le16(bytes + 28, PVR_CELL_ASSET_KEY_BYTES);
    write_le32(bytes + 32, (uint32_t)cell_bytes);
    write_le32(bytes + 36, (uint32_t)stream_bytes);
    write_le32(bytes + 40,
               (uint32_t)(key_count * PVR_CELL_ASSET_KEY_BYTES));
    write_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET, crc32_bytes(
        bytes + PVR_CELL_ASSET_HEADER_BYTES,
        required - PVR_CELL_ASSET_HEADER_BYTES));
    write_le32(bytes + HEADER_CRC_OFFSET,
               crc32_bytes(bytes, HEADER_CRC_BYTES));
    if(written_bytes)
        *written_bytes = required;
    return 0;
}

int pvr_cell_asset_open(const void *data, size_t size,
                        pvr_cell_asset_view_t *view) {
    const uint8_t *bytes = data;
    pvr_cell_asset_view_t parsed;
    uint32_t file_bytes;
    uint32_t cell_count;
    uint32_t stream_count;
    uint32_t key_count;
    uint32_t cell_bytes;
    uint32_t stream_bytes;
    uint32_t key_bytes;
    uint64_t payload_bytes;
    size_t next_key = 0;
    size_t index;

    if(!data || !view || !range_valid(data, size, 1u) ||
       ((uintptr_t)view & (_Alignof(pvr_cell_asset_view_t) - 1u)) ||
       !range_valid(view, 1u, sizeof(*view)) ||
       ranges_overlap(data, size, view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(size < PVR_CELL_ASSET_HEADER_BYTES ||
       read_le32(bytes) != PVR_CELL_ASSET_MAGIC ||
       read_le16(bytes + 4) != PVR_CELL_ASSET_VERSION ||
       read_le16(bytes + 6) != PVR_CELL_ASSET_HEADER_BYTES ||
       read_le16(bytes + 24) != PVR_CELL_ASSET_STATE_BYTES ||
       read_le16(bytes + 26) != PVR_CELL_ASSET_STREAM_BYTES ||
       read_le16(bytes + 28) != PVR_CELL_ASSET_KEY_BYTES ||
       read_le16(bytes + 30) ||
       !bytes_zero(bytes + HEADER_RESERVED_OFFSET, 12u) ||
       read_le32(bytes + HEADER_CRC_OFFSET) !=
           crc32_bytes(bytes, HEADER_CRC_BYTES)) {
        errno = EILSEQ;
        return -1;
    }
    file_bytes = read_le32(bytes + 8);
    cell_count = read_le32(bytes + 12);
    stream_count = read_le32(bytes + 16);
    key_count = read_le32(bytes + 20);
    cell_bytes = read_le32(bytes + 32);
    stream_bytes = read_le32(bytes + 36);
    key_bytes = read_le32(bytes + 40);
    if(file_bytes != size || !cell_count ||
       cell_count > UINT32_MAX / PVR_CELL_ASSET_STATE_BYTES ||
       stream_count > UINT32_MAX / PVR_CELL_ASSET_STREAM_BYTES ||
       key_count > UINT32_MAX / PVR_CELL_ASSET_KEY_BYTES ||
       cell_bytes != cell_count * PVR_CELL_ASSET_STATE_BYTES ||
       stream_bytes != stream_count * PVR_CELL_ASSET_STREAM_BYTES ||
       key_bytes != key_count * PVR_CELL_ASSET_KEY_BYTES) {
        errno = EILSEQ;
        return -1;
    }
    payload_bytes = (uint64_t)cell_bytes + stream_bytes + key_bytes;
    if(payload_bytes != file_bytes - PVR_CELL_ASSET_HEADER_BYTES ||
       read_le32(bytes + HEADER_PAYLOAD_CRC_OFFSET) != crc32_bytes(
           bytes + PVR_CELL_ASSET_HEADER_BYTES, (size_t)payload_bytes)) {
        errno = EILSEQ;
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    parsed.data = data;
    parsed.size = size;
    parsed.cells = bytes + PVR_CELL_ASSET_HEADER_BYTES;
    parsed.cell_count = cell_count;
    parsed.streams = (const uint8_t *)parsed.cells + cell_bytes;
    parsed.stream_count = stream_count;
    parsed.keys = (const uint8_t *)parsed.streams + stream_bytes;
    parsed.key_count = key_count;

    for(index = 0; index < parsed.cell_count; ++index) {
        pvr_cell_state_t state;

        decode_state((const uint8_t *)parsed.cells +
                         index * PVR_CELL_ASSET_STATE_BYTES,
                     &state);
        if(!state_valid(&state)) {
            errno = EILSEQ;
            return -1;
        }
    }
    for(index = 0; index < parsed.stream_count; ++index) {
        pvr_cell_asset_stream_t stream;
        float previous_time = 0.0f;
        size_t key;

        decode_stream((const uint8_t *)parsed.streams +
                          index * PVR_CELL_ASSET_STREAM_BYTES,
                      &stream);
        if(stream.first_key != next_key ||
           stream.key_count > parsed.key_count - next_key ||
           !isfinite(stream.time_offset) || !isfinite(stream.time_max) ||
           stream.time_max <= 0.0f || stream.repeat > 1u) {
            errno = EILSEQ;
            return -1;
        }
        for(key = 0; key < stream.key_count; ++key) {
            pvr_cell_key_t decoded;

            if(decode_key((const uint8_t *)parsed.keys +
                              (next_key + key) * PVR_CELL_ASSET_KEY_BYTES,
                          &decoded) < 0 ||
               decoded.slot_index >= parsed.cell_count ||
               decoded.time < 0.0f ||
               (stream.repeat ? decoded.time >= stream.time_max :
                                decoded.time > stream.time_max) ||
               (key && decoded.time < previous_time)) {
                errno = EILSEQ;
                return -1;
            }
            previous_time = decoded.time;
        }
        next_key += stream.key_count;
    }
    if(next_key != parsed.key_count) {
        errno = EILSEQ;
        return -1;
    }
    *view = parsed;
    return 0;
}

static int reopen_view(const pvr_cell_asset_view_t *view,
                       pvr_cell_asset_view_t *checked) {
    if(!view ||
       ((uintptr_t)view & (_Alignof(pvr_cell_asset_view_t) - 1u)) ||
       !range_valid(view, 1u, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(pvr_cell_asset_open(view->data, view->size, checked) < 0)
        return -1;
    if(view->cells != checked->cells ||
       view->cell_count != checked->cell_count ||
       view->streams != checked->streams ||
       view->stream_count != checked->stream_count ||
       view->keys != checked->keys || view->key_count != checked->key_count) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int pvr_cell_asset_state_get(const pvr_cell_asset_view_t *view, size_t index,
                             pvr_cell_state_t *state) {
    pvr_cell_asset_view_t checked;

    if(!state ||
       ((uintptr_t)state & (_Alignof(pvr_cell_state_t) - 1u)) ||
       !range_valid(state, 1u, sizeof(*state))) {
        errno = EINVAL;
        return -1;
    }
    if(reopen_view(view, &checked) < 0)
        return -1;
    if(ranges_overlap(state, sizeof(*state), checked.data, checked.size) ||
       ranges_overlap(state, sizeof(*state), view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(index >= checked.cell_count) {
        errno = ERANGE;
        return -1;
    }
    decode_state((const uint8_t *)checked.cells +
                     index * PVR_CELL_ASSET_STATE_BYTES,
                 state);
    return 0;
}

int pvr_cell_asset_stream_get(const pvr_cell_asset_view_t *view, size_t index,
                              pvr_cell_asset_stream_t *stream) {
    pvr_cell_asset_view_t checked;

    if(!stream ||
       ((uintptr_t)stream & (_Alignof(pvr_cell_asset_stream_t) - 1u)) ||
       !range_valid(stream, 1u, sizeof(*stream))) {
        errno = EINVAL;
        return -1;
    }
    if(reopen_view(view, &checked) < 0)
        return -1;
    if(ranges_overlap(stream, sizeof(*stream), checked.data, checked.size) ||
       ranges_overlap(stream, sizeof(*stream), view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(index >= checked.stream_count) {
        errno = ERANGE;
        return -1;
    }
    decode_stream((const uint8_t *)checked.streams +
                      index * PVR_CELL_ASSET_STREAM_BYTES,
                  stream);
    return 0;
}

int pvr_cell_asset_key_get(const pvr_cell_asset_view_t *view, size_t index,
                           pvr_cell_key_t *key) {
    pvr_cell_asset_view_t checked;

    if(!key || ((uintptr_t)key & (_Alignof(pvr_cell_key_t) - 1u)) ||
       !range_valid(key, 1u, sizeof(*key))) {
        errno = EINVAL;
        return -1;
    }
    if(reopen_view(view, &checked) < 0)
        return -1;
    if(ranges_overlap(key, sizeof(*key), checked.data, checked.size) ||
       ranges_overlap(key, sizeof(*key), view, sizeof(*view))) {
        errno = EINVAL;
        return -1;
    }
    if(index >= checked.key_count) {
        errno = ERANGE;
        return -1;
    }
    return decode_key((const uint8_t *)checked.keys +
                          index * PVR_CELL_ASSET_KEY_BYTES,
                      key);
}

int pvr_cell_asset_materialize(
        const pvr_cell_asset_view_t *view,
        pvr_cell_state_t *cells, size_t cell_capacity,
        pvr_cell_key_t *keys, size_t key_capacity,
        pvr_cell_stream_view_t *streams, size_t stream_capacity,
        pvr_cell_asset_runtime_t *runtime) {
    pvr_cell_asset_view_t checked;
    pvr_cell_asset_runtime_t materialized;
    size_t cell_bytes;
    size_t key_bytes;
    size_t stream_bytes;
    size_t index;

    if(!runtime ||
       ((uintptr_t)runtime & (_Alignof(pvr_cell_asset_runtime_t) - 1u)) ||
       !range_valid(runtime, 1u, sizeof(*runtime))) {
        errno = EINVAL;
        return -1;
    }
    if(reopen_view(view, &checked) < 0)
        return -1;
    if(!cells || (checked.key_count && !keys) ||
       (checked.stream_count && !streams) ||
       ((uintptr_t)cells & (_Alignof(pvr_cell_state_t) - 1u)) ||
       (keys && ((uintptr_t)keys & (_Alignof(pvr_cell_key_t) - 1u))) ||
       (streams && ((uintptr_t)streams &
                    (_Alignof(pvr_cell_stream_view_t) - 1u))) ||
       !range_valid(cells, checked.cell_count, sizeof(*cells)) ||
       !range_valid(keys, checked.key_count, sizeof(*keys)) ||
       !range_valid(streams, checked.stream_count, sizeof(*streams))) {
        errno = EINVAL;
        return -1;
    }
    if(cell_capacity < checked.cell_count ||
       key_capacity < checked.key_count ||
       stream_capacity < checked.stream_count) {
        errno = ENOSPC;
        return -1;
    }
    cell_bytes = checked.cell_count * sizeof(*cells);
    key_bytes = checked.key_count * sizeof(*keys);
    stream_bytes = checked.stream_count * sizeof(*streams);
    if(ranges_overlap(cells, cell_bytes, keys, key_bytes) ||
       ranges_overlap(cells, cell_bytes, streams, stream_bytes) ||
       ranges_overlap(keys, key_bytes, streams, stream_bytes) ||
       ranges_overlap(cells, cell_bytes, checked.data, checked.size) ||
       ranges_overlap(keys, key_bytes, checked.data, checked.size) ||
       ranges_overlap(streams, stream_bytes, checked.data, checked.size) ||
       ranges_overlap(runtime, sizeof(*runtime), checked.data, checked.size) ||
       ranges_overlap(runtime, sizeof(*runtime), view, sizeof(*view)) ||
       ranges_overlap(cells, cell_bytes, view, sizeof(*view)) ||
       ranges_overlap(keys, key_bytes, view, sizeof(*view)) ||
       ranges_overlap(streams, stream_bytes, view, sizeof(*view)) ||
       ranges_overlap(runtime, sizeof(*runtime), cells, cell_bytes) ||
       ranges_overlap(runtime, sizeof(*runtime), keys, key_bytes) ||
       ranges_overlap(runtime, sizeof(*runtime), streams, stream_bytes)) {
        errno = EINVAL;
        return -1;
    }

    for(index = 0; index < checked.cell_count; ++index)
        decode_state((const uint8_t *)checked.cells +
                         index * PVR_CELL_ASSET_STATE_BYTES,
                     &cells[index]);
    for(index = 0; index < checked.key_count; ++index) {
        if(decode_key((const uint8_t *)checked.keys +
                          index * PVR_CELL_ASSET_KEY_BYTES,
                      &keys[index]) < 0) {
            errno = EPROTO;
            return -1;
        }
    }
    for(index = 0; index < checked.stream_count; ++index) {
        pvr_cell_asset_stream_t descriptor;
        pvr_cell_stream_t source;

        decode_stream((const uint8_t *)checked.streams +
                          index * PVR_CELL_ASSET_STREAM_BYTES,
                      &descriptor);
        source.keys = keys ? keys + descriptor.first_key : NULL;
        source.key_count = descriptor.key_count;
        source.time_offset = descriptor.time_offset;
        source.time_max = descriptor.time_max;
        source.repeat = descriptor.repeat;
        if(pvr_cell_stream_open(&source, checked.cell_count,
                                &streams[index]) < 0) {
            errno = EPROTO;
            return -1;
        }
    }
    materialized.base_cells = cells;
    materialized.cell_count = checked.cell_count;
    materialized.stream_list.streams = streams;
    materialized.stream_list.stream_count = checked.stream_count;
    *runtime = materialized;
    return 0;
}
