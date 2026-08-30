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

#include <dc/matrix.h>

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
    /** Import-only deferred polygon-list capture marker. */
    PVR_CHUNK_CONTROL_CACHE_POLYGONS = 4,
    /** Import-only deferred polygon-list execution marker. */
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
    PVR_CHUNK_VERTEX_XYZ_METADATA = 37,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_565 = 38,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_4444 = 39,
    PVR_CHUNK_VERTEX_XYZ_INTENSITY = 40,
    PVR_CHUNK_VERTEX_XYZ_NORMAL = 41,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_ARGB = 42,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_USER = 43,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_METADATA = 44,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_565 = 45,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_DIFFUSE_4444 = 46,
    PVR_CHUNK_VERTEX_XYZ_NORMAL_INTENSITY = 47,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL = 48,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_ARGB = 49,
    PVR_CHUNK_VERTEX_XYZ_PACKED_NORMAL_USER = 50,
    PVR_CHUNK_VERTEX_XYZ_DIFFUSE_SPECULAR_ARGB = 51,
    PVR_CHUNK_VERTEX_XYZ_METADATA_ARGB = 52,
    PVR_CHUNK_SHAPE_NORMAL = 128,
    PVR_CHUNK_SHAPE_NORMAL_ARGB = 129,
    PVR_CHUNK_SHAPE_ARGB = 130
} pvr_chunk_vertex_type_t;

/** \brief Polygon strip record types understood by the validator.

    The original UV8/UV10 families retain unsigned-normalized decoding for
    existing streams. New content should use the `_FIXED` families: UV8 is
    signed 8.8 fixed point and UV10 is signed 6.10 fixed point. Both occupy
    one 16-bit word per coordinate.
*/
typedef enum pvr_chunk_strip_type {
    PVR_CHUNK_STRIP_INDEX = 64,
    /** Legacy unsigned-normalized UVs in `[0, 1]`. */
    PVR_CHUNK_STRIP_UV8 = 65,
    /** Legacy unsigned-normalized UVs in `[0, 1]`. */
    PVR_CHUNK_STRIP_UV10 = 66,
    PVR_CHUNK_STRIP_NORMAL = 67,
    PVR_CHUNK_STRIP_UV8_NORMAL = 68,
    PVR_CHUNK_STRIP_UV10_NORMAL = 69,
    PVR_CHUNK_STRIP_ARGB = 70,
    PVR_CHUNK_STRIP_UV8_ARGB = 71,
    PVR_CHUNK_STRIP_UV10_ARGB = 72,
    PVR_CHUNK_STRIP_TWO_VOLUME = 73,
    PVR_CHUNK_STRIP_UV8_TWO_VOLUME = 74,
    PVR_CHUNK_STRIP_UV10_TWO_VOLUME = 75,
    /** Signed 8.8 fixed-point UVs with a `1 / 256` step. */
    PVR_CHUNK_STRIP_UV8_FIXED = 76,
    /** Signed 6.10 fixed-point UVs with a `1 / 1024` step. */
    PVR_CHUNK_STRIP_UV10_FIXED = 77,
    PVR_CHUNK_STRIP_UV8_FIXED_NORMAL = 78,
    PVR_CHUNK_STRIP_UV10_FIXED_NORMAL = 79,
    PVR_CHUNK_STRIP_UV8_FIXED_ARGB = 80,
    PVR_CHUNK_STRIP_UV10_FIXED_ARGB = 81,
    PVR_CHUNK_STRIP_UV8_FIXED_TWO_VOLUME = 82,
    PVR_CHUNK_STRIP_UV10_FIXED_TWO_VOLUME = 83
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

/** \brief Runtime requirements discovered during model validation. */
typedef enum pvr_chunk_model_requirement {
    /** Deferred polygon controls must be canonicalized before rendering. */
    PVR_CHUNK_MODEL_REQUIRES_POLYGON_CANONICALIZATION = 1u << 0
} pvr_chunk_model_requirement_t;

/** \brief Summary produced by complete model validation.

    Deferred polygon controls remain structurally inspectable so import tools
    can recognize them, but they describe execution ordering across a complete
    hierarchy rather than reusable geometry inside one model. Their presence
    sets PVR_CHUNK_MODEL_REQUIRES_POLYGON_CANONICALIZATION. Runtime emitters
    reject such streams before side effects; a host compiler must resolve the
    complete deformation and polygon order into ordinary compact strips.
*/
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
    size_t polygon_cache_records;
    size_t polygon_draw_records;
    size_t maximum_strip_vertices; /**< Minimum strip workspace capacity. */
    uint32_t maximum_vertex_index;
    uint32_t requirements;         /**< pvr_chunk_model_requirement_t bits. */
} pvr_chunk_model_info_t;

/** \brief Validated, immutable view of one compact model.

    Create this with pvr_chunk_model_open(). The source streams must remain
    immutable and accessible for the lifetime of the view.
*/
typedef struct pvr_chunk_model_view {
    pvr_chunk_model_t model;
    pvr_chunk_model_info_t info;
} pvr_chunk_model_view_t;

/** \brief Number of vertex indices covered by one prepared index page. */
#define PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE 256u

/** \brief Number of pages spanning the compact model's 16-bit index space. */
#define PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT 256u

/** \brief One direct vertex lookup entry in caller-owned preparation memory.

    A zero type marks an unused index. The word offset is relative to the
    prepared model's vertex stream, so moving either source stream invalidates
    the plan even when its contents remain unchanged.
*/
typedef struct pvr_chunk_vertex_index_entry {
    size_t word_offset;
    uint8_t type;
    uint8_t flags;
    uint16_t reserved;
} pvr_chunk_vertex_index_entry_t;

/** \brief Exact caller-owned storage required by a prepared model plan. */
typedef struct pvr_chunk_model_plan_requirements {
    size_t indexed_pages;
    size_t vertex_index_entries;
    size_t vertex_index_bytes;
} pvr_chunk_model_plan_requirements_t;

/** \brief Prepared immutable model view with constant-time vertex lookup.

    The plan owns no memory. Its model streams and \a vertex_index array remain
    caller-owned and must stay immutable and accessible while the plan is in
    use. Page slots contain one plus the corresponding page number in the
    compact caller array; zero denotes an absent 256-index page.
*/
typedef struct pvr_chunk_model_plan {
    pvr_chunk_model_view_t view;
    const pvr_chunk_vertex_index_entry_t *vertex_index;
    size_t vertex_index_count;
    size_t indexed_page_count;
    uint16_t vertex_page_slots[PVR_CHUNK_VERTEX_INDEX_PAGE_COUNT];
} pvr_chunk_model_plan_t;

/** \brief Typed view of one vertex record. */
typedef struct pvr_chunk_vertex_batch {
    uint8_t type;
    uint8_t flags;
    uint16_t first_index;
    const uint32_t *entries;
    size_t entry_count;
    size_t entry_word_count;
} pvr_chunk_vertex_batch_t;

/** \brief Safely decoded position and raw words for one vertex entry. */
typedef struct pvr_chunk_vertex_view {
    uint16_t index;
    uint8_t position_components;
    const uint32_t *words;
    size_t word_count;
    float position[4];
} pvr_chunk_vertex_view_t;

/** \brief Decoded vertex attributes present in a compact vertex entry. */
typedef enum pvr_chunk_vertex_attribute {
    PVR_CHUNK_VERTEX_ATTR_NORMAL = 1u << 0,
    PVR_CHUNK_VERTEX_ATTR_DIFFUSE_COLOR = 1u << 1,
    PVR_CHUNK_VERTEX_ATTR_SPECULAR_COLOR = 1u << 2,
    PVR_CHUNK_VERTEX_ATTR_DIFFUSE_INTENSITY = 1u << 3,
    PVR_CHUNK_VERTEX_ATTR_SPECULAR_INTENSITY = 1u << 4,
    PVR_CHUNK_VERTEX_ATTR_USER_DATA = 1u << 5,
    PVR_CHUNK_VERTEX_ATTR_METADATA = 1u << 6
} pvr_chunk_vertex_attribute_t;

/** \brief Format-neutral attributes decoded from one vertex entry.

    Colors use `0xAARRGGBB`. Intensities are normalized to `[0, 1]`.
    Metadata remains an uninterpreted 32-bit application/model value; it is
    not assumed to describe a complete skinning influence.
*/
typedef struct pvr_chunk_vertex_attributes {
    uint16_t index;
    uint32_t present;
    point_t position;
    vector_t normal;
    uint32_t diffuse_argb;
    uint32_t specular_argb;
    float diffuse_intensity;
    float specular_intensity;
    uint32_t user_data;
    uint32_t metadata;
} pvr_chunk_vertex_attributes_t;

/** \brief Caller-owned iterator over strips inside one strip record. */
typedef struct pvr_chunk_strip_iterator {
    uint8_t type;
    uint8_t flags;
    const uint16_t *cursor;
    size_t remaining_words;
    size_t remaining_strips;
    size_t vertex_word_count;
    size_t user_word_count;
} pvr_chunk_strip_iterator_t;

/** \brief One bounded strip returned by pvr_chunk_strip_iterator_next(). */
typedef struct pvr_chunk_strip_view {
    uint8_t type;
    uint8_t flags;
    uint8_t reversed;
    const uint16_t *words;
    size_t word_count;
    size_t vertex_count;
    size_t vertex_word_count;
    size_t user_word_count;
} pvr_chunk_strip_view_t;

/** \brief One indexed vertex reference inside a strip. */
typedef struct pvr_chunk_strip_vertex_view {
    uint16_t index;
    const uint16_t *attribute_words;
    size_t attribute_word_count;
    const uint16_t *triangle_user_words;
    size_t triangle_user_word_count;
} pvr_chunk_strip_vertex_view_t;

/** \brief Decoded attributes present on one strip vertex reference. */
typedef enum pvr_chunk_strip_attribute {
    PVR_CHUNK_STRIP_ATTR_UV0 = 1u << 0,
    PVR_CHUNK_STRIP_ATTR_UV1 = 1u << 1,
    PVR_CHUNK_STRIP_ATTR_NORMAL = 1u << 2,
    PVR_CHUNK_STRIP_ATTR_COLOR = 1u << 3
} pvr_chunk_strip_attribute_t;

/** \brief Format-neutral attributes decoded from one strip reference. */
typedef struct pvr_chunk_strip_attributes {
    uint16_t index;
    uint32_t present;
    float uv[2][2];
    vector_t normal;
    uint32_t argb;
    const uint16_t *triangle_user_words;
    size_t triangle_user_word_count;
} pvr_chunk_strip_attributes_t;

/** \brief One triangle decoded from a compact volume record.

    Quads and strips are expanded deterministically. The user-word view points
    into the immutable source record and remains valid for as long as that
    record's stream remains valid.
*/
typedef struct pvr_chunk_volume_triangle {
    uint16_t index[3];
    const uint16_t *user_words;
    size_t user_word_count;
    uint8_t source_type;
    uint8_t final_in_record;
} pvr_chunk_volume_triangle_t;

/** \brief Caller-owned iterator over triangles in one volume record.

    Applications should treat every field as private. Initialization validates
    the complete record framing before the first triangle can be returned.
*/
typedef struct pvr_chunk_volume_iterator {
    uint8_t type;
    uint8_t index_count;
    uint8_t quad_second;
    uint8_t strip_active;
    const uint16_t *cursor;
    size_t remaining_primitives;
    size_t remaining_triangles;
    size_t user_word_count;
    pvr_chunk_strip_iterator_t strip_iterator;
    pvr_chunk_strip_view_t strip;
    size_t strip_triangle;
} pvr_chunk_volume_iterator_t;

/** \brief Sentinel identifying a root node in a compact-model hierarchy. */
#define PVR_CHUNK_NODE_NONE SIZE_MAX

/** \brief One caller-owned node in parent-before-child order.

    A NULL model creates a transform-only grouping node. Non-NULL model views
    and their source streams must remain immutable during traversal.
*/
typedef struct pvr_chunk_hierarchy_node {
    const pvr_chunk_model_view_t *model;
    matrix_t local_transform;
    size_t parent_index;
    const void *user_data;
} pvr_chunk_hierarchy_node_t;

/** \brief Bounded caller-owned hierarchy description. */
typedef struct pvr_chunk_hierarchy {
    const pvr_chunk_hierarchy_node_t *nodes;
    size_t node_count;
} pvr_chunk_hierarchy_t;

/** \brief Traversal progress published on completion or callback stop. */
typedef struct pvr_chunk_hierarchy_result {
    size_t visited_nodes;
} pvr_chunk_hierarchy_result_t;

/** \brief Callback for a node with its composed world transform.

    Return zero to continue, a positive value to stop successfully, or a
    negative value to fail. A failing callback should set errno.
*/
typedef int (*pvr_chunk_hierarchy_visit_t)(
    size_t node_index, const pvr_chunk_hierarchy_node_t *node,
    const matrix_t *world_transform, void *data);

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

/** \brief Validate a model and publish an immutable typed view. */
int pvr_chunk_model_open(const pvr_chunk_model_t *model,
                         pvr_chunk_model_view_t *view);

/** \brief Query exact preparation storage for an admitted model.

    Only pages containing at least one defined vertex consume index entries.
    Every present page requires PVR_CHUNK_VERTEX_INDEX_PAGE_SIZE entries.
    Failure initializes \p requirements to zero.
*/
int pvr_chunk_model_plan_query(
    const pvr_chunk_model_view_t *view,
    pvr_chunk_model_plan_requirements_t *requirements);

/** \brief Build a constant-time vertex plan in caller-owned memory.

    The complete model is admitted again before \p vertex_index is modified.
    The source streams, \p vertex_index, and \p plan must not overlap. The
    required entry count is returned by pvr_chunk_model_plan_query(). Extra
    caller capacity is neither retained nor modified.

    \retval 0  Plan built successfully.
    \retval -1 Invalid, malformed, overlapping, or insufficient storage with
               errno set to EINVAL, EILSEQ, ERANGE, or ENOSPC.
*/
int pvr_chunk_model_plan_build(
    const pvr_chunk_model_view_t *view,
    pvr_chunk_vertex_index_entry_t *vertex_index,
    size_t vertex_index_capacity,
    pvr_chunk_model_plan_t *plan);

/** \brief Decode one validated vertex record into a bounded batch view. */
int pvr_chunk_vertex_batch_decode(const pvr_chunk_record_t *record,
                                  pvr_chunk_vertex_batch_t *batch);

/** \brief Return one vertex entry without reading beyond its batch. */
int pvr_chunk_vertex_batch_get(const pvr_chunk_vertex_batch_t *batch,
                               size_t entry,
                               pvr_chunk_vertex_view_t *vertex);

/** \brief Decode all recognized attributes from one bounded vertex entry.

    Packed normals and colors are expanded to floating vectors and ARGB8888.
    Failure initializes \p attributes to zero.
*/
int pvr_chunk_vertex_attributes_get(
    const pvr_chunk_vertex_batch_t *batch, size_t entry,
    pvr_chunk_vertex_attributes_t *attributes);

/** \brief Resolve and decode one indexed vertex from an admitted model.

    Vertex ranges are unique after pvr_chunk_model_open(), so resolution is
    deterministic. This allocation-free lookup scans the bounded vertex
    records; renderers that need a persistent index should build one in
    caller-owned memory.

    \retval 0  Vertex found and decoded.
    \retval -1 Invalid view or absent index, with errno set to EINVAL,
               EILSEQ, or ENOENT.
*/
int pvr_chunk_model_vertex_attributes_get(
    const pvr_chunk_model_view_t *view, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes);

/** \brief Resolve one indexed vertex through a prepared constant-time plan. */
int pvr_chunk_model_plan_vertex_attributes_get(
    const pvr_chunk_model_plan_t *plan, uint16_t index,
    pvr_chunk_vertex_attributes_t *attributes);

/** \brief Initialize an iterator over a validated polygon strip record. */
int pvr_chunk_strip_iterator_init(pvr_chunk_strip_iterator_t *iterator,
                                  const pvr_chunk_record_t *record);

/** \brief Return the next complete strip.

    \retval 1  A strip was returned.
    \retval 0  All declared strips were already returned.
    \retval -1 Invalid or inconsistent framing.
*/
int pvr_chunk_strip_iterator_next(pvr_chunk_strip_iterator_t *iterator,
                                  pvr_chunk_strip_view_t *strip);

/** \brief Return one indexed vertex reference from a bounded strip. */
int pvr_chunk_strip_vertex_get(const pvr_chunk_strip_view_t *strip,
                               size_t vertex_index,
                               pvr_chunk_strip_vertex_view_t *vertex);

/** \brief Decode UV, normal, color, and triangle-user attributes.

    Eight-bit and ten-bit UV encodings are normalized to `[0, 1]`. Signed
    strip normals are expanded to `[-1, 1]`. Failure initializes
    \p attributes to zero.
*/
int pvr_chunk_strip_attributes_get(
    const pvr_chunk_strip_view_t *strip, size_t vertex_index,
    pvr_chunk_strip_attributes_t *attributes);

/** \brief Count triangles in one structurally valid volume record.

    Quads count as two triangles and strips count as `vertex_count - 2`.
    Failure initializes \p count to zero.
*/
int pvr_chunk_volume_triangle_count(const pvr_chunk_record_t *record,
                                    size_t *count);

/** \brief Initialize a bounded triangle iterator for one volume record.

    This performs a complete framing pass, so a successful iterator cannot
    encounter a truncated primitive later unless its immutable source is
    modified concurrently.
*/
int pvr_chunk_volume_iterator_init(pvr_chunk_volume_iterator_t *iterator,
                                   const pvr_chunk_record_t *record);

/** \brief Return the next triangle from a volume record.

    \retval 1  A triangle was returned.
    \retval 0  All triangles were already returned.
    \retval -1 Invalid iterator state or mutated source framing.
*/
int pvr_chunk_volume_iterator_next(pvr_chunk_volume_iterator_t *iterator,
                                   pvr_chunk_volume_triangle_t *triangle);

/** \brief Compose and visit a bounded parent-before-child hierarchy.

    The complete hierarchy is checked before the first callback or workspace
    write. Each parent index must be PVR_CHUNK_NODE_NONE or less than the
    current node index, which rejects cycles and forward references without a
    visited bitmap. The caller supplies one `matrix_t`-aligned matrix per node.

    \param hierarchy       Nodes to traverse in array order.
    \param root_transform  Optional transform applied above every root.
    \param world_matrices  Caller-owned output/workspace for composed matrices.
    \param world_capacity  Number of matrices available in the workspace.
    \param visit           Optional callback invoked once per visited node.
    \param data            Opaque callback data.
    \param result          Optional traversal progress result.

    \retval 0  Every node was visited.
    \retval 1  A callback requested a successful early stop.
    \retval -1 Invalid input, arithmetic failure, or callback failure.
*/
int pvr_chunk_hierarchy_traverse(
    const pvr_chunk_hierarchy_t *hierarchy,
    const matrix_t *root_transform,
    matrix_t *world_matrices, size_t world_capacity,
    pvr_chunk_hierarchy_visit_t visit, void *data,
    pvr_chunk_hierarchy_result_t *result);

/** \brief Compose a hierarchy using caller-supplied local transforms.

    This is the animation-binding form of pvr_chunk_hierarchy_traverse(). The
    node array continues to provide topology, model, and user data, while one
    caller-owned local matrix per node replaces the static local matrices in
    the hierarchy. This permits output from anim_clip_sample_matrices() to feed
    traversal directly without copying or mutating the model hierarchy.

    The complete topology and every selected local matrix are validated before
    the first callback or workspace write. Local and world arrays may be the
    same exact array for canonical in-place composition; any other overlap is
    rejected.

    \param hierarchy        Nodes whose topology and bindings are traversed.
    \param local_transforms One local matrix per node.
    \param local_capacity   Number of matrices in \p local_transforms.
    \param root_transform   Optional transform applied above every root.
    \param world_matrices   Caller-owned output/workspace.
    \param world_capacity   Number of matrices in the workspace.
    \param visit            Optional callback invoked once per visited node.
    \param data             Opaque callback data.
    \param result           Optional traversal progress result.

    \retval 0  Every node was visited.
    \retval 1  A callback requested a successful early stop.
    \retval -1 Invalid input, arithmetic failure, or callback failure.
*/
int pvr_chunk_hierarchy_traverse_transforms(
    const pvr_chunk_hierarchy_t *hierarchy,
    const matrix_t *local_transforms, size_t local_capacity,
    const matrix_t *root_transform,
    matrix_t *world_matrices, size_t world_capacity,
    pvr_chunk_hierarchy_visit_t visit, void *data,
    pvr_chunk_hierarchy_result_t *result);

/** @} */

__END_DECLS

#endif /* __DC_PVR_CHUNK_MODEL_H */
