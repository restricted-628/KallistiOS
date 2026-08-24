/* KallistiOS ##version##

   dc/pvr_chunk_model.h
   Copyright (C) 2026 Joseph Black
*/

/** \file    dc/pvr_chunk_model.h
    \brief   Bounded views and validation for compact PVR model streams.
    \ingroup pvr_chunk_model

    Chunk models store indexed vertices and polygon state in two compact,
    self-delimiting streams. This interface adds the byte bounds absent from
    pointer-only model descriptions and validates the complete model before a
    renderer consumes it. It does not allocate memory or own model data.
*/

#ifndef __DC_PVR_CHUNK_MODEL_H
#define __DC_PVR_CHUNK_MODEL_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stddef.h>
#include <stdint.h>

/** \defgroup pvr_chunk_model Compact PVR models
    \brief                         Checked compact model streams
    \ingroup                       pvr_geometry
    @{
*/

/** \brief Stream containing a chunk record. */
typedef enum pvr_chunk_stream_kind {
    PVR_CHUNK_STREAM_VERTEX = 0,  /**< 32-bit vertex records. */
    PVR_CHUNK_STREAM_POLYGON     /**< 16-bit state and polygon records. */
} pvr_chunk_stream_kind_t;

/** \brief Structural record family. */
typedef enum pvr_chunk_record_class {
    PVR_CHUNK_RECORD_NULL = 0,
    PVR_CHUNK_RECORD_BITS,
    PVR_CHUNK_RECORD_TEXTURE,
    PVR_CHUNK_RECORD_MATERIAL,
    PVR_CHUNK_RECORD_VERTEX,
    PVR_CHUNK_RECORD_VOLUME,
    PVR_CHUNK_RECORD_STRIP,
    PVR_CHUNK_RECORD_SHAPE,
    PVR_CHUNK_RECORD_END
} pvr_chunk_record_class_t;

/** \brief Single-word stream control types. */
typedef enum pvr_chunk_control_type {
    PVR_CHUNK_CONTROL_NULL = 0,
    PVR_CHUNK_CONTROL_BLEND = 1,
    PVR_CHUNK_CONTROL_MIPMAP_ADJUST = 2,
    PVR_CHUNK_CONTROL_SPECULAR_EXPONENT = 3,
    PVR_CHUNK_CONTROL_CACHE_POLYGONS = 4,
    PVR_CHUNK_CONTROL_DRAW_CACHED_POLYGONS = 5,
    PVR_CHUNK_CONTROL_END = 255
} pvr_chunk_control_type_t;

/** \brief Two-word texture-state record types. */
typedef enum pvr_chunk_texture_type {
    PVR_CHUNK_TEXTURE = 8,
    PVR_CHUNK_TEXTURE_TWO_VOLUME = 9
} pvr_chunk_texture_type_t;

/** \brief Material record types. */
typedef enum pvr_chunk_material_type {
    PVR_CHUNK_MATERIAL_DIFFUSE = 17,
    PVR_CHUNK_MATERIAL_AMBIENT = 18,
    PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT = 19,
    PVR_CHUNK_MATERIAL_SPECULAR = 20,
    PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR = 21,
    PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR = 22,
    PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR = 23,
    PVR_CHUNK_MATERIAL_BUMP = 24,
    PVR_CHUNK_MATERIAL_DIFFUSE_TWO_VOLUME = 25,
    PVR_CHUNK_MATERIAL_AMBIENT_TWO_VOLUME = 26,
    PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_TWO_VOLUME = 27,
    PVR_CHUNK_MATERIAL_SPECULAR_TWO_VOLUME = 28,
    PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR_TWO_VOLUME = 29,
    PVR_CHUNK_MATERIAL_AMBIENT_SPECULAR_TWO_VOLUME = 30,
    PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR_TWO_VOLUME = 31
} pvr_chunk_material_type_t;

/** \brief Vertex record types understood by the validator. */
typedef enum pvr_chunk_vertex_type {
    PVR_CHUNK_VERTEX_XYZW = 32,
    PVR_CHUNK_VERTEX_XYZW_NORMAL = 33,
    PVR_CHUNK_VERTEX_XYZ = 34,
    PVR_CHUNK_VERTEX_XYZ_ARGB = 35,
    PVR_CHUNK_VERTEX_XYZ_USER = 36,
    PVR_CHUNK_VERTEX_XYZ_WEIGHT = 37,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565 = 38,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444 = 39,
    PVR_CHUNK_VERTEX_XYZ_INTENSITY = 40,
    PVR_CHUNK_VERTEX_XYZ_NORMAL = 41,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB = 42,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_USER = 43,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_WEIGHT = 44,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_565 = 45,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_4444 = 46,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY = 47,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL = 48,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_ARGB = 49,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER = 50,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_SPECULAR_ARGB = 51,
    PVR_CHUNK_VERTEX_XYZ_WEIGHT_ARGB = 52,
    PVR_CHUNK_SHAPE_NORMAL = 128,
    PVR_CHUNK_SHAPE_NORMAL_ARGB = 129,
    PVR_CHUNK_SHAPE_ARGB = 130
} pvr_chunk_vertex_type_t;

/** \brief Polygon strip record types understood by the validator. */
typedef enum pvr_chunk_strip_type {
    PVR_CHUNK_STRIP_INDEX = 64,
    PVR_CHUNK_STRIP_UV8 = 65,
    PVR_CHUNK_STRIP_UV10 = 66,
    PVR_CHUNK_STRIP_NORMAL = 67,
    PVR_CHUNK_STRIP_UV8_NORMAL = 68,
    PVR_CHUNK_STRIP_UV10_NORMAL = 69,
    PVR_CHUNK_STRIP_ARGB = 70,
    PVR_CHUNK_STRIP_UV8_ARGB = 71,
    PVR_CHUNK_STRIP_UV10_ARGB = 72,
    PVR_CHUNK_STRIP_TWO_VOLUME = 73,
    PVR_CHUNK_STRIP_UV8_TWO_VOLUME = 74,
    PVR_CHUNK_STRIP_UV10_TWO_VOLUME = 75
} pvr_chunk_strip_type_t;

/** \brief Modifier-volume record types. */
typedef enum pvr_chunk_volume_type {
    PVR_CHUNK_VOLUME_TRIANGLES = 56,
    PVR_CHUNK_VOLUME_QUADS = 57,
    PVR_CHUNK_VOLUME_STRIPS = 58
} pvr_chunk_volume_type_t;

/** \brief Flags returned in pvr_chunk_record_t::flags for strip records. */
typedef enum pvr_chunk_strip_flags {
    PVR_CHUNK_STRIP_IGNORE_LIGHT = 0x01,
    PVR_CHUNK_STRIP_IGNORE_SPECULAR = 0x02,
    PVR_CHUNK_STRIP_IGNORE_AMBIENT = 0x04,
    PVR_CHUNK_STRIP_USE_ALPHA = 0x08,
    PVR_CHUNK_STRIP_DOUBLE_SIDED = 0x10,
    PVR_CHUNK_STRIP_FLAT_SHADED = 0x20,
    PVR_CHUNK_STRIP_ENVIRONMENT = 0x40
} pvr_chunk_strip_flags_t;

/** \brief A bounded compact model description.

    Counts are expressed in the natural word size of each stream. Both streams
    must include a final end record. The center and radius are used by later
    traversal and visibility layers and are validated here with the streams.
*/
typedef struct pvr_chunk_model {
    const uint32_t *vertex_words;
    size_t vertex_word_count;
    const uint16_t *polygon_words;
    size_t polygon_word_count;
    float center[3];
    float radius;
} pvr_chunk_model_t;

/** \brief One safely bounded record returned by an iterator. */
typedef struct pvr_chunk_record {
    pvr_chunk_stream_kind_t stream;
    pvr_chunk_record_class_t record_class;
    uint8_t type;
    uint8_t flags;
    const void *words;          /**< First natural word of the record. */
    size_t word_count;          /**< Complete record size in natural words. */
    const void *payload;        /**< First word after the record header. */
    size_t payload_word_count;  /**< Payload size in natural words. */
    size_t stream_word_offset;  /**< Record offset in its source stream. */
} pvr_chunk_record_t;

/** \brief Caller-owned iterator state.

    Applications should treat fields other than kind as private. A successful
    next operation returns the end record once; the following call returns 0.
*/
typedef struct pvr_chunk_iterator {
    pvr_chunk_stream_kind_t kind;
    const void *words;
    size_t word_count;
    size_t offset;
    int ended;
} pvr_chunk_iterator_t;

/** \brief Summary produced by complete model validation. */
typedef struct pvr_chunk_model_info {
    size_t vertex_records;
    size_t vertex_entries;
    size_t shape_records;
    size_t polygon_records;
    size_t material_records;
    size_t strip_records;
    size_t strips;
    size_t triangles;
    size_t index_references;
    uint32_t maximum_vertex_index;
} pvr_chunk_model_info_t;

/** \brief Initialize an iterator for a bounded vertex stream. */
int pvr_chunk_vertex_iterator_init(pvr_chunk_iterator_t *iterator,
                                   const uint32_t *words,
                                   size_t word_count);

/** \brief Initialize an iterator for a bounded polygon stream. */
int pvr_chunk_polygon_iterator_init(pvr_chunk_iterator_t *iterator,
                                    const uint16_t *words,
                                    size_t word_count);

/** \brief Return the next complete record without reading beyond the stream.

    \retval 1  A record was returned.
    \retval 0  The end record was already returned.
    \retval -1 Invalid or truncated stream, with errno set to EINVAL or
               EILSEQ.
*/
int pvr_chunk_iterator_next(pvr_chunk_iterator_t *iterator,
                            pvr_chunk_record_t *record);

/** \brief Validate an entire bounded model.

    Validation checks record lengths and terminators, exact per-format vertex
    sizes, finite vertex data and bounds, strip/volume framing, overflow-safe
    counters, and that every polygon index names a vertex present in the
    vertex stream. Failed validation initializes \p info to zero when supplied.

    \retval 0  The model is structurally valid.
    \retval -1 Invalid model, with errno set to EINVAL, EILSEQ, or ERANGE.
*/
int pvr_chunk_model_validate(const pvr_chunk_model_t *model,
                             pvr_chunk_model_info_t *info);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_MODEL_H */
