/* KallistiOS ##version##

   Host-side source to compact PVR model converter.
   Copyright (C) 2026 Joseph Black

   The source boundary is intentionally narrower than OBJ as a whole. Vertex
   positions become indexed 32-bit records. Per-corner UVs and normals become
   16-bit strip attributes, so independently indexed OBJ attributes do not
   require vertex duplication. OBJ faces must already be triangles; glTF's
   explicitly defined triangle, strip, and fan topology is lowered without
   guessing polygon triangulation or material-name policy.

   Emission is planned in memory, split before compact 16-bit fields overflow,
   and admitted by pvr_chunk_model_validate() before temporary files are
   created. Output words are serialized explicitly little-endian so host byte
   order cannot alter the asset.
*/

#define _POSIX_C_SOURCE 200809L

#include <dc/pvr_chunk_asset.h>
#include <dc/pvr_chunk_cache.h>
#include <dc/pvr_chunk_cache_asset.h>
#include <dc/pvr_chunk_model.h>
#include <dc/pvr_chunk_scene.h>
#include <dc/pvr_chunk_skeleton_asset.h>
#include <dc/pvr_chunk_skin_asset.h>
#include <dc/pvr_chunk_shape_asset.h>
#include <dc/pvr_chunk_animation_asset.h>
#include <dc/pvr_chunk_volume_asset.h>
#include <dc/pvr_chunk_resource_asset.h>
#include <dc/pvr_chunk_texture_asset.h>

#include "pvr-scene-ir.h"
#include "third_party/cgltf.h"
#include "stb_image.h"

#include <kos/pvr_chunk_asset_lz4.h>

#include <lz4frame.h>

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_POSITION_COUNT 65536u
#define MAX_VERTEX_BATCH ((UINT16_MAX - 1u) / 3u)
#define MAX_STRIP_COUNT UINT16_C(0x3fff)
#define MAX_C_SYMBOL_LENGTH 31u

#define MATERIAL_DIFFUSE  (1u << 0)
#define MATERIAL_AMBIENT  (1u << 1)
#define MATERIAL_SPECULAR (1u << 2)
#define MATERIAL_EXPONENT (1u << 3)
#define MATERIAL_ALPHA    (1u << 4)

/* The host compiler links the target cache builder but never submits PVR
   packets. These unreachable publication hooks satisfy that shared object's
   host link without introducing a second cache implementation. */
void pvr_mod_compile(pvr_mod_hdr_t *destination, pvr_list_t list,
                     uint32_t mode, uint32_t culling) {
    (void)destination;
    (void)list;
    (void)mode;
    (void)culling;
}

int pvr_prim(const void *data, size_t size) {
    (void)data;
    (void)size;
    errno = ENOTSUP;
    return -1;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    return pvr_prim(data, size);
}

typedef struct source_position {
    float value[3];
} source_position_t;

typedef struct source_texcoord {
    float value[2];
} source_texcoord_t;

typedef struct gltf_texture_mapping {
    int texture_identifier;
    int texcoord_set;
    float offset[2];
    float scale[2];
    float rotation;
    int transformed;
} gltf_texture_mapping_t;

typedef struct source_normal {
    float value[3];
} source_normal_t;

typedef struct source_color {
    float value[4];
} source_color_t;

typedef struct source_corner {
    size_t position;
    size_t texcoord;
    size_t normal;
} source_corner_t;

typedef struct source_triangle {
    source_corner_t corner[3];
    uint8_t strip_type;
    int texture_identifier;
    size_t material_definition;
} source_triangle_t;

typedef struct material_binding {
    char *name;
    int texture_identifier;
} material_binding_t;

typedef struct material_table {
    material_binding_t *bindings;
    size_t count;
    size_t capacity;
} material_table_t;

typedef struct material_definition {
    char *name;
    float diffuse[3];
    float ambient[3];
    float specular[3];
    float exponent;
    float alpha;
    uint8_t alpha_mode;
    uint8_t double_sided;
    unsigned int present;
} material_definition_t;

enum {
    MATERIAL_ALPHA_OPAQUE = 0,
    MATERIAL_ALPHA_MASK,
    MATERIAL_ALPHA_BLEND
};

typedef struct material_library {
    material_definition_t *definitions;
    size_t count;
    size_t capacity;
    char **paths;
    size_t file_count;
    size_t path_capacity;
} material_library_t;

typedef struct source_model {
    source_position_t *positions;
    size_t position_count;
    size_t position_capacity;
    source_texcoord_t *texcoords;
    size_t texcoord_count;
    size_t texcoord_capacity;
    source_normal_t *normals;
    size_t normal_count;
    size_t normal_capacity;
    source_color_t *colors;
    size_t color_count;
    size_t color_capacity;
    int has_colors;
    source_triangle_t *triangles;
    size_t triangle_count;
    size_t triangle_capacity;
} source_model_t;

typedef struct source_model_set {
    source_model_t *models;
    size_t count;
} source_model_set_t;

typedef struct gltf_model_metadata {
    pvr_chunk_skin_span_t *skin_spans;
    pvr_chunk_skin_weight_t *skin_weights;
    pvr_chunk_skin_general_t skin;
    pvr_chunk_skeleton_joint_t *skeleton_joints;
    pvr_chunk_skeleton_t skeleton;
    pvr_chunk_shape_target_t *shape_targets;
    pvr_chunk_shape_delta_t *shape_deltas;
    pvr_chunk_shape_set_t shapes;
} gltf_model_metadata_t;

typedef struct gltf_animation_metadata {
    char *name;
    anim_vector_hermite_key_t *animation_vector_keys;
    anim_quaternion_hermite_key_t *animation_quaternion_keys;
    anim_track_view_t *animation_tracks;
    anim_transform_tracks_t *animation_transforms;
    anim_visibility_tracks_t *animation_visibility;
    anim_clip_view_t animation;
    size_t animation_track_count;
    size_t animation_key_count;
    anim_scalar_hermite_key_t *morph_animation_keys;
    anim_track_view_t *morph_animation_tracks;
    pvr_chunk_shape_channel_t *morph_animation_channels;
    pvr_chunk_morph_animation_binding_t *morph_animation_bindings;
    pvr_chunk_morph_animation_t morph_animation;
    size_t morph_animation_track_count;
    size_t morph_animation_key_count;
} gltf_animation_metadata_t;

typedef struct gltf_asset_metadata {
    gltf_model_metadata_t *models;
    size_t model_count;
    gltf_animation_metadata_t *animations;
    size_t animation_count;
    uint8_t *texture_section;
    size_t texture_section_bytes;
} gltf_asset_metadata_t;

static int serialize_gltf_textures(
    const cgltf_data *data, const char *source_path,
    int texture_override, uint8_t **bytes_out, size_t *size_out);

typedef struct source_strip {
    size_t first_triangle;
    size_t triangle_count;
    size_t vertex_count;
    uint8_t strip_type;
    int texture_identifier;
    size_t material_definition;
} source_strip_t;

typedef struct strip_plan {
    source_strip_t *strips;
    size_t count;
    size_t capacity;
} strip_plan_t;

typedef struct output_streams {
    uint32_t *vertex_words;
    size_t vertex_word_count;
    uint16_t *polygon_words;
    size_t polygon_word_count;
    float center[3];
    float radius;
    size_t strip_record_count;
    size_t texture_record_count;
    size_t material_record_count;
    size_t source_strip_count;
    size_t output_strip_count;
} output_streams_t;

typedef struct temporary_output {
    char *path;
} temporary_output_t;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [--flip-winding] [--flip-v] [--texture-id ID | "
            "--material NAME=ID ...] [--material-library FILE ...] "
            "[--join-strips] [--emit-c SYMBOL | --emit-asset "
            "[--section-directory [--scene-root] [--rigid-skin] "
            "[--morph-target DX DY DZ] [--animation-offset DX DY DZ] "
            "[--cooked-cache]] "
            "[--lz4-vertices]] "
            "[--] INPUT.{obj,gltf,glb} "
            "{VERTICES.bin POLYGONS.bin | MODEL.c | MODEL.pcm}\n",
            program);
}

static void source_model_free(source_model_t *model) {
    free(model->positions);
    free(model->texcoords);
    free(model->normals);
    free(model->colors);
    free(model->triangles);
    memset(model, 0, sizeof(*model));
}

static void source_model_set_free(source_model_set_t *set) {
    size_t model;

    if(!set)
        return;
    for(model = 0; model < set->count; ++model)
        source_model_free(&set->models[model]);
    free(set->models);
    memset(set, 0, sizeof(*set));
}

static void gltf_model_metadata_free(gltf_model_metadata_t *metadata) {
    if(!metadata)
        return;
    free(metadata->skin_spans);
    free(metadata->skin_weights);
    free(metadata->skeleton_joints);
    free(metadata->shape_targets);
    free(metadata->shape_deltas);
    memset(metadata, 0, sizeof(*metadata));
}

static void gltf_animation_metadata_free(
    gltf_animation_metadata_t *metadata) {
    if(!metadata)
        return;
    free(metadata->name);
    free(metadata->animation_vector_keys);
    free(metadata->animation_quaternion_keys);
    free(metadata->animation_tracks);
    free(metadata->animation_transforms);
    free(metadata->animation_visibility);
    free(metadata->morph_animation_keys);
    free(metadata->morph_animation_tracks);
    free(metadata->morph_animation_channels);
    free(metadata->morph_animation_bindings);
    memset(metadata, 0, sizeof(*metadata));
}

static void gltf_asset_metadata_free(gltf_asset_metadata_t *metadata) {
    size_t model;
    size_t animation;

    if(!metadata)
        return;
    for(model = 0; model < metadata->model_count; ++model)
        gltf_model_metadata_free(&metadata->models[model]);
    for(animation = 0; animation < metadata->animation_count; ++animation)
        gltf_animation_metadata_free(&metadata->animations[animation]);
    free(metadata->models);
    free(metadata->animations);
    free(metadata->texture_section);
    memset(metadata, 0, sizeof(*metadata));
}

static void output_streams_free(output_streams_t *streams) {
    free(streams->vertex_words);
    free(streams->polygon_words);
    memset(streams, 0, sizeof(*streams));
}

static void strip_plan_free(strip_plan_t *plan) {
    free(plan->strips);
    memset(plan, 0, sizeof(*plan));
}

static void material_table_free(material_table_t *table) {
    size_t binding;

    for(binding = 0; binding < table->count; ++binding)
        free(table->bindings[binding].name);
    free(table->bindings);
    memset(table, 0, sizeof(*table));
}

static void material_library_free(material_library_t *library) {
    size_t definition;
    size_t file;

    for(definition = 0; definition < library->count; ++definition)
        free(library->definitions[definition].name);
    for(file = 0; file < library->file_count; ++file)
        free(library->paths[file]);
    free(library->definitions);
    free(library->paths);
    memset(library, 0, sizeof(*library));
}

static int reserve_array(void **array, size_t *capacity, size_t required,
                         size_t element_size) {
    size_t next;
    void *allocation;

    if(required <= *capacity)
        return 0;
    next = *capacity ? *capacity : 64u;
    while(next < required) {
        if(next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    if(next > SIZE_MAX / element_size) {
        errno = EOVERFLOW;
        return -1;
    }
    allocation = realloc(*array, next * element_size);
    if(!allocation) {
        errno = ENOMEM;
        return -1;
    }

    *array = allocation;
    *capacity = next;
    return 0;
}

static int read_line(FILE *file, char **line, size_t *capacity,
                     size_t *length) {
    size_t used = 0;
    int character;

    /* Even an empty physical line needs storage for its terminator. */
    if(!*line || !*capacity) {
        *line = malloc(256u);
        if(!*line) {
            errno = ENOMEM;
            return -1;
        }
        *capacity = 256u;
    }

    for(;;) {
        character = fgetc(file);
        if(character == EOF) {
            if(ferror(file))
                return -1;
            if(!used)
                return 0;
            break;
        }
        if(character == '\n')
            break;
        if(used + 1u >= *capacity) {
            size_t next = *capacity ? *capacity * 2u : 256u;
            char *allocation;

            if(next <= *capacity) {
                errno = EOVERFLOW;
                return -1;
            }
            allocation = realloc(*line, next);
            if(!allocation) {
                errno = ENOMEM;
                return -1;
            }
            *line = allocation;
            *capacity = next;
        }
        (*line)[used++] = (char)character;
    }

    if(used && (*line)[used - 1u] == '\r')
        --used;
    (*line)[used] = '\0';
    *length = used;
    return 1;
}

static char *next_token(char **cursor) {
    char *start = *cursor;
    char *end;

    while(*start && isspace((unsigned char)*start))
        ++start;
    if(!*start || *start == '#') {
        *cursor = start;
        return NULL;
    }

    end = start;
    while(*end && !isspace((unsigned char)*end) && *end != '#')
        ++end;
    if(*end == '#') {
        *end = '\0';
        *cursor = end;
    }
    else if(*end) {
        *end = '\0';
        *cursor = end + 1u;
    }
    else
        *cursor = end;
    return start;
}

static int parse_float_token(const char *text, float *result) {
    char *end;
    float value;

    errno = 0;
    value = strtof(text, &end);
    if(errno == ERANGE || end == text || *end || !isfinite(value)) {
        errno = EILSEQ;
        return -1;
    }
    *result = value;
    return 0;
}

static int parse_texture_identifier(const char *text, int *result) {
    char *end;
    unsigned long value;

    if(!*text || *text == '-') {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if(errno == ERANGE || end == text || *end || value > 0x1fffu) {
        errno = EINVAL;
        return -1;
    }
    *result = (int)value;
    return 0;
}

static int valid_c_symbol(const char *symbol) {
    static const char *const keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default",
        "do", "double", "else", "enum", "extern", "float", "for", "goto",
        "if", "inline", "int", "long", "register", "restrict", "return",
        "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while",
        "alignas", "alignof", "thread_local"
    };
    size_t length = strlen(symbol);
    size_t character;
    size_t keyword;

    if(!length || length > MAX_C_SYMBOL_LENGTH ||
       !isalpha((unsigned char)symbol[0]))
        return 0;
    for(character = 1; character < length; ++character) {
        if(!isalnum((unsigned char)symbol[character]) &&
           symbol[character] != '_')
            return 0;
    }
    for(keyword = 0; keyword < sizeof(keywords) / sizeof(keywords[0]);
        ++keyword) {
        if(!strcmp(symbol, keywords[keyword]))
            return 0;
    }
    return 1;
}

static int material_table_add(material_table_t *table, const char *text) {
    const char *equals = strrchr(text, '=');
    material_binding_t binding;
    void *allocation = table->bindings;
    size_t name_length;
    size_t existing;

    if(!equals || equals == text || !equals[1] || strchr(text, '=') != equals) {
        errno = EINVAL;
        return -1;
    }
    name_length = (size_t)(equals - text);
    for(existing = 0; existing < name_length; ++existing) {
        if(isspace((unsigned char)text[existing]) || text[existing] == '#') {
            errno = EINVAL;
            return -1;
        }
    }
    binding.name = malloc(name_length + 1u);
    if(!binding.name) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(binding.name, text, name_length);
    binding.name[name_length] = '\0';
    if(parse_texture_identifier(equals + 1u,
                                &binding.texture_identifier) < 0) {
        free(binding.name);
        return -1;
    }
    for(existing = 0; existing < table->count; ++existing) {
        if(!strcmp(binding.name, table->bindings[existing].name)) {
            free(binding.name);
            errno = EEXIST;
            return -1;
        }
    }
    if(reserve_array(&allocation, &table->capacity, table->count + 1u,
                     sizeof(*table->bindings)) < 0) {
        free(binding.name);
        return -1;
    }
    table->bindings = allocation;
    table->bindings[table->count++] = binding;
    return 0;
}

static int material_table_find(const material_table_t *table,
                               const char *name, int *texture_identifier) {
    size_t binding;

    for(binding = 0; binding < table->count; ++binding) {
        if(!strcmp(name, table->bindings[binding].name)) {
            *texture_identifier =
                table->bindings[binding].texture_identifier;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int material_definition_find(const material_library_t *library,
                                    const char *name, size_t *result) {
    size_t definition;

    for(definition = 0; definition < library->count; ++definition) {
        if(!strcmp(name, library->definitions[definition].name)) {
            *result = definition;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int material_definition_add(material_library_t *library,
                                   const char *name,
                                   material_definition_t **result) {
    material_definition_t definition = { 0 };
    void *allocation = library->definitions;
    size_t ignored;

    if(material_definition_find(library, name, &ignored) == 0) {
        errno = EILSEQ;
        return -1;
    }
    errno = 0;
    definition.alpha = 1.0f;
    definition.alpha_mode = MATERIAL_ALPHA_OPAQUE;
    definition.name = strdup(name);
    if(!definition.name) {
        errno = ENOMEM;
        return -1;
    }
    if(reserve_array(&allocation, &library->capacity, library->count + 1u,
                     sizeof(*library->definitions)) < 0) {
        free(definition.name);
        return -1;
    }
    library->definitions = allocation;
    library->definitions[library->count] = definition;
    *result = &library->definitions[library->count++];
    return 0;
}

static int parse_material_color(char *cursor, float color[3]) {
    size_t component;

    for(component = 0; component < 3u; ++component) {
        char *token = next_token(&cursor);

        if(!token || parse_float_token(token, &color[component]) < 0)
            return -1;
        if(color[component] < 0.0f || color[component] > 1.0f) {
            errno = ERANGE;
            return -1;
        }
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int parse_material_exponent(char *cursor, float *exponent) {
    char *token = next_token(&cursor);

    if(!token || parse_float_token(token, exponent) < 0)
        return -1;
    if(*exponent < 0.0f || *exponent > 1000.0f) {
        errno = ERANGE;
        return -1;
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int parse_material_opacity(char *cursor, int inverted,
                                  material_definition_t *definition) {
    char *token = next_token(&cursor);
    float opacity;

    if(!token || parse_float_token(token, &opacity) < 0)
        return -1;
    if(opacity < 0.0f || opacity > 1.0f) {
        errno = ERANGE;
        return -1;
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    if(inverted)
        opacity = 1.0f - opacity;
    definition->alpha = opacity;
    definition->alpha_mode = opacity < 1.0f ?
        MATERIAL_ALPHA_BLEND : MATERIAL_ALPHA_OPAQUE;
    definition->present |= MATERIAL_ALPHA;
    return 0;
}

static int load_material_library(const char *path,
                                 material_library_t *library,
                                 size_t *error_line) {
    FILE *file = fopen(path, "r");
    material_definition_t *active = NULL;
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_length;
    size_t line_number = 0;
    size_t initial_count = library->count;
    int rv;
    int saved_errno;

    if(!file)
        return -1;
    errno = 0;
    while((rv = read_line(file, &line, &line_capacity, &line_length)) > 0) {
        char *cursor = line;
        char *directive;
        unsigned int property = 0;
        float *color = NULL;

        (void)line_length;
        ++line_number;
        directive = next_token(&cursor);
        if(!directive)
            continue;
        if(!strcmp(directive, "newmtl")) {
            char *name = next_token(&cursor);

            if(!name || next_token(&cursor)) {
                errno = EILSEQ;
                rv = -1;
            }
            else if(material_definition_add(library, name, &active) < 0)
                rv = -1;
            else
                rv = 0;
        }
        else {
            if(!active) {
                errno = EILSEQ;
                rv = -1;
            }
            else if(!strcmp(directive, "Kd")) {
                property = MATERIAL_DIFFUSE;
                color = active->diffuse;
            }
            else if(!strcmp(directive, "Ka")) {
                property = MATERIAL_AMBIENT;
                color = active->ambient;
            }
            else if(!strcmp(directive, "Ks")) {
                property = MATERIAL_SPECULAR;
                color = active->specular;
            }
            else if(!strcmp(directive, "Ns")) {
                property = MATERIAL_EXPONENT;
                if(active->present & property) {
                    errno = EILSEQ;
                    rv = -1;
                }
                else {
                    rv = parse_material_exponent(cursor, &active->exponent);
                    if(!rv)
                        active->present |= property;
                }
            }
            else if(!strcmp(directive, "d") ||
                    !strcmp(directive, "Tr")) {
                property = MATERIAL_ALPHA;
                if(active->present & property) {
                    errno = EILSEQ;
                    rv = -1;
                }
                else
                    rv = parse_material_opacity(
                        cursor, !strcmp(directive, "Tr"), active);
            }
            else {
                errno = ENOTSUP;
                rv = -1;
            }

            if(color) {
                if(active->present & property) {
                    errno = EILSEQ;
                    rv = -1;
                }
                else {
                    rv = parse_material_color(cursor, color);
                    if(!rv)
                        active->present |= property;
                }
            }
        }
        if(rv < 0) {
            *error_line = line_number;
            goto fail;
        }
    }
    if(rv < 0) {
        if(!errno)
            errno = EIO;
        *error_line = line_number + 1u;
        goto fail;
    }
    if(library->count == initial_count) {
        errno = EILSEQ;
        *error_line = line_number ? line_number : 1u;
        goto fail;
    }
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    file = NULL;
    {
        void *allocation = library->paths;
        char *path_copy = strdup(path);

        if(!path_copy) {
            errno = ENOMEM;
            goto fail;
        }
        if(reserve_array(&allocation, &library->path_capacity,
                         library->file_count + 1u,
                         sizeof(*library->paths)) < 0) {
            free(path_copy);
            goto fail;
        }
        library->paths = allocation;
        library->paths[library->file_count++] = path_copy;
    }
    free(line);
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    free(line);
    errno = saved_errno;
    return -1;
}

static int material_definition_complete(
    const material_definition_t *definition) {
    unsigned int specular = definition->present & MATERIAL_SPECULAR;
    unsigned int exponent = definition->present & MATERIAL_EXPONENT;

    return (definition->present & MATERIAL_DIFFUSE) &&
           !!specular == !!exponent;
}

static int triangle_type(const source_model_t *model,
                         source_triangle_t *triangle);
static int validate_references(const source_model_t *model);

static int path_has_suffix(const char *path, const char *suffix) {
    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    size_t index;

    if(path_length < suffix_length)
        return 0;
    path += path_length - suffix_length;
    for(index = 0; index < suffix_length; ++index) {
        if(tolower((unsigned char)path[index]) !=
           tolower((unsigned char)suffix[index]))
            return 0;
    }
    return 1;
}

static int source_is_gltf(const char *path) {
    return path_has_suffix(path, ".gltf") || path_has_suffix(path, ".glb");
}

static int cgltf_errno(cgltf_result result) {
    switch(result) {
        case cgltf_result_file_not_found:
            return ENOENT;
        case cgltf_result_out_of_memory:
            return ENOMEM;
        case cgltf_result_io_error:
            return EIO;
        case cgltf_result_unknown_format:
        case cgltf_result_legacy_gltf:
            return ENOTSUP;
        case cgltf_result_invalid_json:
        case cgltf_result_invalid_gltf:
        case cgltf_result_data_too_short:
            return EILSEQ;
        default:
            return EIO;
    }
}

static const cgltf_accessor *gltf_attribute(
    const cgltf_primitive *primitive, cgltf_attribute_type type,
    cgltf_int index) {
    cgltf_size attribute;

    for(attribute = 0; attribute < primitive->attributes_count;
        ++attribute) {
        if(primitive->attributes[attribute].type == type &&
           primitive->attributes[attribute].index == index)
            return primitive->attributes[attribute].data;
    }
    return NULL;
}

/* cgltf cross-links are pointers into its decoded arrays. Convert them to
   ordinals without relying on relational comparison of unrelated pointers if
   a malformed document or future cgltf change violates that provenance. */
static int gltf_array_index(const void *base, size_t count,
                            size_t element_size, const void *entry,
                            size_t *index) {
    uintptr_t begin = (uintptr_t)base;
    uintptr_t address = (uintptr_t)entry;
    size_t span;
    uintptr_t offset;

    if(!base || !entry || !element_size ||
       count > SIZE_MAX / element_size) {
        errno = EILSEQ;
        return -1;
    }
    span = count * element_size;
    if(address < begin || address - begin >= span) {
        errno = EILSEQ;
        return -1;
    }
    offset = address - begin;
    if(offset % element_size) {
        errno = EILSEQ;
        return -1;
    }
    *index = (size_t)(offset / element_size);
    return 0;
}

static size_t gltf_mesh_ordinal(const cgltf_mesh *const *meshes,
                                size_t mesh_count,
                                const cgltf_mesh *mesh) {
    size_t index;

    for(index = 0; index < mesh_count; ++index) {
        if(meshes[index] == mesh)
            return index;
    }
    return SIZE_MAX;
}

static int gltf_scene_collect_meshes(const cgltf_node *node,
                                     const cgltf_mesh **meshes,
                                     const cgltf_skin **skins,
                                     size_t *mesh_count,
                                     size_t mesh_capacity,
                                     size_t *visited, size_t node_limit) {
    cgltf_size child;
    size_t ordinal;

    if(++*visited > node_limit) {
        errno = EILSEQ;
        return -1;
    }
    if(node->has_mesh_gpu_instancing) {
        errno = ENOTSUP;
        return -1;
    }
    if(node->mesh) {
        ordinal = gltf_mesh_ordinal(meshes, *mesh_count, node->mesh);
        if(ordinal == SIZE_MAX) {
            /* First selected-scene traversal order is the stable model
               ordinal used by both hierarchy nodes and PCM2 stream pairs. */
            if(*mesh_count >= mesh_capacity) {
                errno = EILSEQ;
                return -1;
            }
            ordinal = (*mesh_count)++;
            meshes[ordinal] = node->mesh;
            skins[ordinal] = node->skin;
        }
        else if(skins[ordinal] != node->skin) {
            errno = ENOTSUP;
            return -1;
        }
    }
    for(child = 0; child < node->children_count; ++child) {
        if(gltf_scene_collect_meshes(
               node->children[child], meshes, skins, mesh_count,
               mesh_capacity, visited, node_limit) < 0)
            return -1;
    }
    return 0;
}

static int gltf_scene_append_node(const cgltf_node *node,
                                  const cgltf_mesh *const *meshes,
                                  size_t mesh_count,
                                  uint32_t parent_index,
                                  pvr_scene_ir_t *scene,
                                  const cgltf_data *data,
                                  size_t *node_to_scene,
                                  size_t *visited, size_t node_limit) {
    float local[16];
    uint32_t node_index;
    size_t node_ordinal;
    size_t model_ordinal;
    cgltf_size child;

    if(++*visited > node_limit || scene->node_count >= UINT32_MAX) {
        errno = EILSEQ;
        return -1;
    }
    cgltf_node_transform_local(node, local);
    node_index = (uint32_t)scene->node_count;
    if(gltf_array_index(data->nodes, data->nodes_count,
                        sizeof(*data->nodes), node, &node_ordinal) < 0)
        return -1;
    node_to_scene[node_ordinal] = node_index;
    model_ordinal = node->mesh ?
        gltf_mesh_ordinal(meshes, mesh_count, node->mesh) : SIZE_MAX;
    if(node->mesh && (model_ordinal == SIZE_MAX ||
                      model_ordinal >= UINT32_MAX)) {
        errno = EILSEQ;
        return -1;
    }
    if(pvr_scene_ir_add_node(
           scene, parent_index,
           node->mesh ? (uint32_t)model_ordinal :
                        PVR_CHUNK_SCENE_MODEL_NONE,
           local) < 0)
        return -1;
    for(child = 0; child < node->children_count; ++child) {
        if(gltf_scene_append_node(
               node->children[child], meshes, mesh_count, node_index,
               scene, data, node_to_scene, visited, node_limit) < 0)
            return -1;
    }
    return 0;
}

static int gltf_build_scene(const cgltf_data *data,
                            const cgltf_scene *source_scene,
                            const cgltf_mesh **meshes,
                            const cgltf_skin **skins,
                            size_t *mesh_count,
                            pvr_scene_ir_t *scene,
                            size_t *node_to_scene) {
    size_t visited = 0;
    cgltf_size root;

    *mesh_count = 0;
    for(root = 0; root < data->nodes_count; ++root)
        node_to_scene[root] = SIZE_MAX;
    for(root = 0; root < source_scene->nodes_count; ++root) {
        if(gltf_scene_collect_meshes(
               source_scene->nodes[root], meshes, skins, mesh_count,
               data->meshes_count, &visited, data->nodes_count) < 0)
            return -1;
    }
    if(!*mesh_count) {
        errno = EILSEQ;
        return -1;
    }
    visited = 0;
    for(root = 0; root < source_scene->nodes_count; ++root) {
        if(gltf_scene_append_node(
               source_scene->nodes[root], meshes, *mesh_count,
               UINT32_MAX, scene, data, node_to_scene, &visited,
               data->nodes_count) < 0)
            return -1;
    }
    if(visited != data->nodes_count) {
        /* PCM2 intentionally serializes the selected scene, not detached
           authoring nodes. Detached nodes may still be referenced by a skin;
           that broader case is rejected when skin bindings are imported. */
        size_t node;

        for(node = 0; node < data->nodes_count; ++node) {
            if(data->nodes[node].skin ||
               gltf_mesh_ordinal(meshes, *mesh_count,
                                 data->nodes[node].mesh) != SIZE_MAX) {
                const cgltf_node *cursor = &data->nodes[node];
                int belongs = 0;

                while(cursor) {
                    for(root = 0; root < source_scene->nodes_count; ++root) {
                        if(cursor == source_scene->nodes[root]) {
                            belongs = 1;
                            break;
                        }
                    }
                    if(belongs)
                        break;
                    cursor = cursor->parent;
                }
                if(!belongs) {
                    errno = ENOTSUP;
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int gltf_add_materials(const cgltf_data *data,
                              material_library_t *library) {
    cgltf_size material;

    for(material = 0; material <= data->materials_count; ++material) {
        const cgltf_material *source = material ?
            &data->materials[material - 1u] : NULL;
        material_definition_t *definition;
        char generated_name[64];
        const char *name;
        const float *base;
        float metallic = 0.0f;
        float roughness = 1.0f;
        size_t component;

        if(source && (source->has_transmission || source->has_volume ||
                      source->has_diffuse_transmission ||
                      source->has_pbr_specular_glossiness ||
                      source->normal_texture.texture ||
                      source->occlusion_texture.texture ||
                      source->emissive_texture.texture ||
                      source->emissive_factor[0] != 0.0f ||
                      source->emissive_factor[1] != 0.0f ||
                      source->emissive_factor[2] != 0.0f ||
                      source->unlit ||
                      source->extensions_count)) {
            errno = ENOTSUP;
            return -1;
        }
        snprintf(generated_name, sizeof(generated_name),
                 "gltf-material-%zu", (size_t)material);
        name = source && source->name && *source->name ?
               source->name : generated_name;
        if(material_definition_add(library, name, &definition) < 0) {
            if(errno != EILSEQ || name == generated_name)
                return -1;
            name = generated_name;
            if(material_definition_add(library, name, &definition) < 0)
                return -1;
        }
        base = source && source->has_pbr_metallic_roughness ?
            source->pbr_metallic_roughness.base_color_factor : NULL;
        if(base) {
            for(component = 0; component < 4; ++component) {
                if(!isfinite(base[component]) || base[component] < 0.0f ||
                   base[component] > 1.0f) {
                    errno = EILSEQ;
                    return -1;
                }
            }
        }
        if(source && source->has_pbr_metallic_roughness) {
            metallic = source->pbr_metallic_roughness.metallic_factor;
            roughness = source->pbr_metallic_roughness.roughness_factor;
            if(source->pbr_metallic_roughness.
                   metallic_roughness_texture.texture ||
               !isfinite(metallic) || !isfinite(roughness) ||
               metallic < 0.0f || metallic > 1.0f ||
               roughness < 0.0f || roughness > 1.0f) {
                errno = ENOTSUP;
                return -1;
            }
        }
        definition->diffuse[0] = base ? base[0] : 1.0f;
        definition->diffuse[1] = base ? base[1] : 1.0f;
        definition->diffuse[2] = base ? base[2] : 1.0f;
        for(component = 0; component < 3; ++component) {
            definition->ambient[component] =
                definition->diffuse[component] * 0.125f;
            definition->specular[component] =
                0.04f * (1.0f - metallic) +
                definition->diffuse[component] * metallic;
        }
        definition->exponent = roughness <= 0.01f ? 1000.0f :
            fminf(1000.0f, 2.0f / powf(roughness, 4.0f) - 2.0f);
        if(definition->exponent < 0.0f)
            definition->exponent = 0.0f;
        definition->double_sided = source && source->double_sided;
        if(source) {
            switch(source->alpha_mode) {
                case cgltf_alpha_mode_opaque:
                    definition->alpha_mode = MATERIAL_ALPHA_OPAQUE;
                    definition->alpha = 1.0f;
                    break;
                case cgltf_alpha_mode_mask:
                    if(source->alpha_cutoff != 0.5f) {
                        errno = ENOTSUP;
                        return -1;
                    }
                    definition->alpha_mode = MATERIAL_ALPHA_MASK;
                    definition->alpha = base ? base[3] : 1.0f;
                    break;
                case cgltf_alpha_mode_blend:
                    definition->alpha_mode = MATERIAL_ALPHA_BLEND;
                    definition->alpha = base ? base[3] : 1.0f;
                    break;
                default:
                    errno = EILSEQ;
                    return -1;
            }
        }
        definition->present = MATERIAL_DIFFUSE | MATERIAL_AMBIENT |
                              MATERIAL_SPECULAR | MATERIAL_EXPONENT;
        if(definition->alpha_mode != MATERIAL_ALPHA_OPAQUE)
            definition->present |= MATERIAL_ALPHA;
    }
    return 0;
}

static int gltf_append_position_value(source_model_t *model,
                                      const float value[3]) {
    void *allocation = model->positions;
    source_position_t position;

    if(model->position_count == MAX_POSITION_COUNT) {
        errno = EOVERFLOW;
        return -1;
    }
    memcpy(position.value, value, sizeof(position.value));
    if(reserve_array(&allocation, &model->position_capacity,
                     model->position_count + 1u,
                     sizeof(*model->positions)) < 0)
        return -1;
    model->positions = allocation;
    model->positions[model->position_count++] = position;
    return 0;
}

static int gltf_append_color_value(source_model_t *model,
                                   const float value[4]) {
    void *allocation = model->colors;
    source_color_t color;
    size_t component;

    for(component = 0; component < 4u; ++component) {
        if(!isfinite(value[component]) || value[component] < 0.0f ||
           value[component] > 1.0f) {
            errno = EILSEQ;
            return -1;
        }
    }
    memcpy(color.value, value, sizeof(color.value));
    if(reserve_array(&allocation, &model->color_capacity,
                     model->color_count + 1u,
                     sizeof(*model->colors)) < 0)
        return -1;
    model->colors = allocation;
    model->colors[model->color_count++] = color;
    return 0;
}

static int gltf_append_texcoord_value(
    source_model_t *model, const float value[2],
    const gltf_texture_mapping_t *mapping, int flip_v) {
    void *allocation = model->texcoords;
    source_texcoord_t texcoord;
    double u = value[0];
    double v = value[1];

    if(!isfinite(u) || !isfinite(v)) {
        errno = EILSEQ;
        return -1;
    }
    if(mapping->transformed) {
        double cosine = cos(mapping->rotation);
        double sine = sin(mapping->rotation);
        double scaled_u = u * mapping->scale[0];
        double scaled_v = v * mapping->scale[1];

        u = mapping->offset[0] + cosine * scaled_u - sine * scaled_v;
        v = mapping->offset[1] + sine * scaled_u + cosine * scaled_v;
    }
    if(flip_v)
        v = 1.0 - v;
    if(!isfinite(u) || !isfinite(v) ||
       u < -FLT_MAX || u > FLT_MAX || v < -FLT_MAX || v > FLT_MAX) {
        errno = ERANGE;
        return -1;
    }

    texcoord.value[0] = (float)u;
    texcoord.value[1] = (float)v;
    if(reserve_array(&allocation, &model->texcoord_capacity,
                     model->texcoord_count + 1u,
                     sizeof(*model->texcoords)) < 0)
        return -1;
    model->texcoords = allocation;
    model->texcoords[model->texcoord_count++] = texcoord;
    return 0;
}

static int gltf_append_normal_value(source_model_t *model,
                                    const float value[3]) {
    void *allocation = model->normals;
    source_normal_t normal;
    double length_squared = 0.0;
    double inverse_length;
    size_t component;

    for(component = 0; component < 3; ++component) {
        length_squared += (double)value[component] * value[component];
    }
    if(!(length_squared > 0.0) || !isfinite(length_squared)) {
        errno = ERANGE;
        return -1;
    }
    inverse_length = 1.0 / sqrt(length_squared);
    for(component = 0; component < 3; ++component)
        normal.value[component] = (float)(value[component] * inverse_length);
    if(reserve_array(&allocation, &model->normal_capacity,
                     model->normal_count + 1u,
                     sizeof(*model->normals)) < 0)
        return -1;
    model->normals = allocation;
    model->normals[model->normal_count++] = normal;
    return 0;
}

static int gltf_material_index(const cgltf_data *data,
                               const cgltf_material *material,
                               size_t *index) {
    if(!material) {
        *index = 0;
        return 0;
    }
    if(gltf_array_index(data->materials, data->materials_count,
                        sizeof(*data->materials), material, index) < 0)
        return -1;
    ++*index;
    return 0;
}

static int gltf_texture_mapping(const cgltf_data *data,
                                const cgltf_material *material,
                                int override,
                                gltf_texture_mapping_t *mapping) {
    const cgltf_texture_view *view = NULL;
    const cgltf_texture *texture = NULL;
    size_t component;

    memset(mapping, 0, sizeof(*mapping));
    mapping->texture_identifier = -1;
    mapping->scale[0] = 1.0f;
    mapping->scale[1] = 1.0f;

    if(override >= 0) {
        mapping->texture_identifier = override;
        return 0;
    }
    if(material && material->has_pbr_metallic_roughness) {
        view = &material->pbr_metallic_roughness.base_color_texture;
        texture = view->texture;
    }
    if(!texture)
        return 0;

    mapping->texcoord_set = view->texcoord;
    if(view->has_transform) {
        if(view->transform.has_texcoord)
            mapping->texcoord_set = view->transform.texcoord;
        for(component = 0; component < 2; ++component) {
            if(!isfinite(view->transform.offset[component]) ||
               !isfinite(view->transform.scale[component])) {
                errno = EILSEQ;
                return -1;
            }
            mapping->offset[component] = view->transform.offset[component];
            mapping->scale[component] = view->transform.scale[component];
        }
        if(!isfinite(view->transform.rotation)) {
            errno = EILSEQ;
            return -1;
        }
        mapping->rotation = view->transform.rotation;
        mapping->transformed = 1;
    }
    if(mapping->texcoord_set < 0) {
        errno = EILSEQ;
        return -1;
    }
    {
        size_t texture_ordinal;

        if(gltf_array_index(data->textures, data->textures_count,
                            sizeof(*data->textures), texture,
                            &texture_ordinal) < 0)
            return -1;
        if(texture_ordinal > UINT16_MAX) {
            errno = EILSEQ;
            return -1;
        }
        mapping->texture_identifier = (int)texture_ordinal;
    }
    return 0;
}

static int gltf_required_extensions_supported(const cgltf_data *data) {
    cgltf_size extension;

    for(extension = 0; extension < data->extensions_required_count;
        ++extension) {
        if(strcmp(data->extensions_required[extension],
                  "KHR_texture_transform")) {
            errno = ENOTSUP;
            return -1;
        }
    }
    return 0;
}

static int gltf_primitive_triangle_count(cgltf_primitive_type type,
                                         cgltf_size element_count,
                                         cgltf_size *triangle_count) {
    switch(type) {
        case cgltf_primitive_type_triangles:
            if(!element_count || element_count % 3u) {
                errno = EILSEQ;
                return -1;
            }
            *triangle_count = element_count / 3u;
            return 0;
        case cgltf_primitive_type_triangle_strip:
        case cgltf_primitive_type_triangle_fan:
            if(element_count < 3u) {
                errno = EILSEQ;
                return -1;
            }
            *triangle_count = element_count - 2u;
            return 0;
        default:
            errno = ENOTSUP;
            return -1;
    }
}

static cgltf_size gltf_primitive_corner_element(
    cgltf_primitive_type type, cgltf_size triangle, size_t corner) {
    if(type == cgltf_primitive_type_triangles)
        return triangle * 3u + corner;
    if(type == cgltf_primitive_type_triangle_fan)
        return corner ? triangle + corner : 0;
    if((triangle & 1u) && corner < 2u)
        return triangle + 1u - corner;
    return triangle + corner;
}

static int gltf_append_primitive(const cgltf_data *data,
                                 const cgltf_primitive *primitive,
                                 source_model_t *model, int flip_winding,
                                 int flip_v, int texture_override,
                                 int has_skin) {
    const cgltf_accessor *positions = gltf_attribute(
        primitive, cgltf_attribute_type_position, 0);
    const cgltf_accessor *texcoords;
    const cgltf_accessor *normals = gltf_attribute(
        primitive, cgltf_attribute_type_normal, 0);
    const cgltf_accessor *colors = gltf_attribute(
        primitive, cgltf_attribute_type_color, 0);
    size_t position_base = model->position_count;
    size_t texcoord_base = model->texcoord_count;
    size_t normal_base = model->normal_count;
    size_t material_index;
    gltf_texture_mapping_t texture_mapping;
    cgltf_size vertex;
    cgltf_size element_count;
    cgltf_size triangle_count;
    cgltf_size triangle_index;
    cgltf_size attribute;

    for(attribute = 0; attribute < primitive->attributes_count;
        ++attribute) {
        const cgltf_attribute *candidate =
            &primitive->attributes[attribute];

        if(candidate->index < 0 ||
           ((candidate->type == cgltf_attribute_type_position ||
             candidate->type == cgltf_attribute_type_normal ||
             candidate->type == cgltf_attribute_type_color) &&
            candidate->index != 0) ||
           (candidate->type != cgltf_attribute_type_position &&
            candidate->type != cgltf_attribute_type_normal &&
            candidate->type != cgltf_attribute_type_texcoord &&
            candidate->type != cgltf_attribute_type_color &&
            candidate->type != cgltf_attribute_type_joints &&
            candidate->type != cgltf_attribute_type_weights)) {
            errno = ENOTSUP;
            return -1;
        }
        if(!has_skin &&
           (candidate->type == cgltf_attribute_type_joints ||
            candidate->type == cgltf_attribute_type_weights)) {
            /* Deformation attributes are authored meaning, not optional
               metadata. Never turn an unbound skin into a rigid mesh. */
            errno = ENOTSUP;
            return -1;
        }
    }

    if(gltf_material_index(data, primitive->material,
                           &material_index) < 0 ||
       gltf_texture_mapping(data, primitive->material, texture_override,
                            &texture_mapping) < 0)
        return -1;
    texcoords = texture_mapping.texture_identifier >= 0 ?
        gltf_attribute(primitive, cgltf_attribute_type_texcoord,
                       texture_mapping.texcoord_set) : NULL;

    if(primitive->has_draco_mesh_compression || !positions ||
       positions->type != cgltf_type_vec3 || !positions->count ||
       (texcoords && (texcoords->type != cgltf_type_vec2 ||
                      texcoords->count != positions->count)) ||
       (normals && (normals->type != cgltf_type_vec3 ||
                    normals->count != positions->count)) ||
       (colors && ((colors->type != cgltf_type_vec3 &&
                    colors->type != cgltf_type_vec4) ||
                   colors->count != positions->count)) ||
       positions->count > MAX_POSITION_COUNT - model->position_count) {
        errno = ENOTSUP;
        return -1;
    }
    if(texture_mapping.texture_identifier >= 0 && !texcoords) {
        errno = EILSEQ;
        return -1;
    }

    for(vertex = 0; vertex < positions->count; ++vertex) {
        float position[3];
        float texcoord[2];
        float normal[3];
        float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float *base = primitive->material &&
            primitive->material->has_pbr_metallic_roughness ?
            primitive->material->pbr_metallic_roughness.base_color_factor :
            NULL;
        size_t component;

        if(!cgltf_accessor_read_float(positions, vertex, position, 3) ||
           gltf_append_position_value(model, position) < 0)
            return -1;
        if(texcoords &&
           (!cgltf_accessor_read_float(texcoords, vertex, texcoord, 2) ||
            gltf_append_texcoord_value(model, texcoord, &texture_mapping,
                                       flip_v) < 0))
            return -1;
        if(normals &&
           (!cgltf_accessor_read_float(normals, vertex, normal, 3) ||
            gltf_append_normal_value(model, normal) < 0))
            return -1;
        if(model->has_colors) {
            if(colors && !cgltf_accessor_read_float(
                             colors, vertex, color,
                             colors->type == cgltf_type_vec3 ? 3u : 4u)) {
                errno = EILSEQ;
                return -1;
            }
            for(component = 0; component < 4u; ++component)
                color[component] *= base ? base[component] : 1.0f;
            if(gltf_append_color_value(model, color) < 0)
                return -1;
        }
    }

    element_count = primitive->indices ?
                    primitive->indices->count : positions->count;
    if(gltf_primitive_triangle_count(primitive->type, element_count,
                                     &triangle_count) < 0)
        return -1;
    for(triangle_index = 0; triangle_index < triangle_count;
        ++triangle_index) {
        source_triangle_t triangle;
        void *allocation = model->triangles;
        size_t corner;

        memset(&triangle, 0, sizeof(triangle));
        for(corner = 0; corner < 3; ++corner) {
            cgltf_size element = gltf_primitive_corner_element(
                primitive->type, triangle_index, corner);
            cgltf_uint index = (cgltf_uint)element;

            if(primitive->indices &&
               !cgltf_accessor_read_uint(primitive->indices,
                                         element, &index, 1)) {
                errno = EILSEQ;
                return -1;
            }
            if(index >= positions->count) {
                errno = EILSEQ;
                return -1;
            }
            triangle.corner[corner].position = position_base + index;
            triangle.corner[corner].texcoord = texcoords ?
                texcoord_base + index : SIZE_MAX;
            triangle.corner[corner].normal = normals ?
                normal_base + index : SIZE_MAX;
        }
        if(flip_winding) {
            source_corner_t temporary = triangle.corner[1];

            triangle.corner[1] = triangle.corner[2];
            triangle.corner[2] = temporary;
        }
        triangle.texture_identifier = texture_mapping.texture_identifier;
        triangle.material_definition = material_index;
        if(triangle_type(model, &triangle) < 0 ||
           reserve_array(&allocation, &model->triangle_capacity,
                         model->triangle_count + 1u,
                         sizeof(*model->triangles)) < 0)
            return -1;
        model->triangles = allocation;
        model->triangles[model->triangle_count++] = triangle;
    }
    return 0;
}

static int gltf_skin_append_vertex(
    const cgltf_primitive *primitive, cgltf_size vertex,
    size_t source_vertex, size_t source_count,
    pvr_scene_ir_deform_contribution_t **contributions,
    size_t *contribution_count, size_t *contribution_capacity) {
    size_t set_count = 0;
    size_t attribute;

    for(attribute = 0; attribute < primitive->attributes_count;
        ++attribute) {
        const cgltf_attribute *candidate =
            &primitive->attributes[attribute];

        if(candidate->type == cgltf_attribute_type_joints) {
            const cgltf_accessor *weight = gltf_attribute(
                primitive, cgltf_attribute_type_weights, candidate->index);

            if(candidate->index < 0 || !candidate->data || !weight ||
               candidate->data->type != cgltf_type_vec4 ||
               weight->type != cgltf_type_vec4 ||
               candidate->data->count != weight->count ||
               vertex >= candidate->data->count) {
                errno = EILSEQ;
                return -1;
            }
            ++set_count;
        }
        else if(candidate->type == cgltf_attribute_type_weights &&
                !gltf_attribute(primitive, cgltf_attribute_type_joints,
                                candidate->index)) {
            errno = EILSEQ;
            return -1;
        }
    }
    if(!set_count || source_vertex > UINT16_MAX) {
        errno = EILSEQ;
        return -1;
    }

    for(attribute = 0; attribute < primitive->attributes_count;
        ++attribute) {
        const cgltf_attribute *joint_attribute =
            &primitive->attributes[attribute];
        const cgltf_accessor *weight_accessor;
        cgltf_uint source_joints[4];
        cgltf_float source_weights[4];
        size_t lane;

        if(joint_attribute->type != cgltf_attribute_type_joints)
            continue;
        weight_accessor = gltf_attribute(
            primitive, cgltf_attribute_type_weights,
            joint_attribute->index);
        if(!cgltf_accessor_read_uint(joint_attribute->data, vertex,
                                     source_joints, 4) ||
           !cgltf_accessor_read_float(weight_accessor, vertex,
                                      source_weights, 4)) {
            errno = EILSEQ;
            return -1;
        }
        for(lane = 0; lane < 4; ++lane) {
            void *allocation = *contributions;
            float value = source_weights[lane];

            if(!isfinite(value) || value < 0.0 ||
               source_joints[lane] >= source_count ||
               source_joints[lane] > UINT16_MAX) {
                errno = EILSEQ;
                return -1;
            }
            if(value == 0.0)
                continue;
            if(reserve_array(&allocation, contribution_capacity,
                             *contribution_count + 1u,
                             sizeof(**contributions)) < 0)
                return -1;
            *contributions = allocation;
            (*contributions)[*contribution_count].vertex_index =
                source_vertex;
            (*contributions)[*contribution_count].source_index =
                source_joints[lane];
            (*contributions)[*contribution_count].weight = value;
            ++*contribution_count;
        }
    }
    return 0;
}

static int gltf_build_skin(const cgltf_data *data,
                           const cgltf_mesh *mesh,
                           const cgltf_skin *skin,
                           const size_t *node_to_scene,
                           const pvr_scene_ir_t *scene,
                           gltf_model_metadata_t *metadata) {
    pvr_scene_ir_deform_source_t *sources = NULL;
    pvr_scene_ir_deform_contribution_t *contributions = NULL;
    pvr_scene_ir_deformation_t canonical = { 0 };
    uint16_t *vertex_indices = NULL;
    size_t contribution_count = 0;
    size_t contribution_capacity = 0;
    size_t vertex_count = 0;
    size_t source_vertex = 0;
    cgltf_size primitive;
    cgltf_size joint;
    int saved_errno;

    if(!skin)
        return 0;
    if(!skin->joints_count || skin->joints_count > UINT16_MAX + 1u ||
       (skin->inverse_bind_matrices &&
        (skin->inverse_bind_matrices->type != cgltf_type_mat4 ||
         skin->inverse_bind_matrices->count != skin->joints_count))) {
        errno = ENOTSUP;
        return -1;
    }
    for(primitive = 0; primitive < mesh->primitives_count; ++primitive) {
        const cgltf_accessor *positions = gltf_attribute(
            &mesh->primitives[primitive],
            cgltf_attribute_type_position, 0);

        if(!positions || positions->count > UINT16_MAX + 1u - vertex_count) {
            errno = positions ? EOVERFLOW : EILSEQ;
            return -1;
        }
        vertex_count += positions->count;
    }
    if(!vertex_count ||
       vertex_count > SIZE_MAX / sizeof(*vertex_indices) ||
       skin->joints_count > SIZE_MAX / sizeof(*sources)) {
        errno = vertex_count ? EOVERFLOW : EILSEQ;
        return -1;
    }
    sources = calloc(skin->joints_count, sizeof(*sources));
    vertex_indices = malloc(vertex_count * sizeof(*vertex_indices));
    if(!sources || !vertex_indices) {
        errno = ENOMEM;
        goto fail;
    }
    for(joint = 0; joint < skin->joints_count; ++joint) {
        const cgltf_node *node = skin->joints[joint];
        size_t node_ordinal;
        float values[16];
        size_t component;

        if(gltf_array_index(data->nodes, data->nodes_count,
                            sizeof(*data->nodes), node,
                            &node_ordinal) < 0)
            goto fail;
        if(node_to_scene[node_ordinal] == SIZE_MAX) {
            errno = ENOTSUP;
            goto fail;
        }
        sources[joint].node_index = node_to_scene[node_ordinal];
        if(skin->inverse_bind_matrices) {
            if(!cgltf_accessor_read_float(
                   skin->inverse_bind_matrices, joint, values, 16)) {
                errno = EILSEQ;
                goto fail;
            }
        }
        else {
            memset(values, 0, sizeof(values));
            values[0] = values[5] = values[10] = values[15] = 1.0f;
        }
        for(component = 0; component < 16; ++component) {
            if(!isfinite(values[component])) {
                errno = EILSEQ;
                goto fail;
            }
            sources[joint].inverse_bind
                [component / 4u][component % 4u] = values[component];
        }
    }

    for(primitive = 0; primitive < mesh->primitives_count; ++primitive) {
        const cgltf_primitive *source = &mesh->primitives[primitive];
        const cgltf_accessor *positions = gltf_attribute(
            source, cgltf_attribute_type_position, 0);
        const cgltf_accessor *joint_zero = gltf_attribute(
            source, cgltf_attribute_type_joints, 0);
        const cgltf_accessor *weight_zero = gltf_attribute(
            source, cgltf_attribute_type_weights, 0);
        cgltf_size vertex;

        if(!positions || !joint_zero || !weight_zero) {
            errno = EILSEQ;
            goto fail;
        }
        for(vertex = 0; vertex < positions->count; ++vertex) {
            vertex_indices[source_vertex + vertex] =
                (uint16_t)(source_vertex + vertex);
            if(gltf_skin_append_vertex(
                   source, vertex, source_vertex + vertex,
                   skin->joints_count, &contributions,
                   &contribution_count, &contribution_capacity) < 0)
                goto fail;
        }
        source_vertex += positions->count;
    }
    if(source_vertex != vertex_count) {
        errno = EPROTO;
        goto fail;
    }
    if(pvr_scene_ir_canonicalize_deformation(
           scene, vertex_indices, vertex_count, sources,
           skin->joints_count, contributions, contribution_count,
           &canonical) < 0) {
        if(errno == EINVAL || errno == EDOM)
            errno = EILSEQ;
        goto fail;
    }

    metadata->skin_spans = canonical.spans;
    metadata->skin_weights = canonical.weights;
    metadata->skin = canonical.skin;
    metadata->skeleton_joints = canonical.joints;
    metadata->skeleton = canonical.skeleton;
    memset(&canonical, 0, sizeof(canonical));
    free(vertex_indices);
    free(contributions);
    free(sources);
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    pvr_scene_ir_deformation_free(&canonical);
    free(vertex_indices);
    free(contributions);
    free(sources);
    errno = saved_errno;
    return -1;
}

static int gltf_target_delta(const cgltf_morph_target *target,
                             cgltf_size vertex,
                             pvr_chunk_shape_delta_t *delta,
                             int *nonzero) {
    const cgltf_accessor *position = NULL;
    const cgltf_accessor *normal = NULL;
    cgltf_size attribute;
    float values[3];
    size_t component;

    memset(delta, 0, sizeof(*delta));
    *nonzero = 0;
    for(attribute = 0; attribute < target->attributes_count; ++attribute) {
        const cgltf_attribute *candidate = &target->attributes[attribute];

        if(candidate->index != 0 || !candidate->data) {
            errno = ENOTSUP;
            return -1;
        }
        if(candidate->type == cgltf_attribute_type_position) {
            if(position) {
                errno = EILSEQ;
                return -1;
            }
            position = candidate->data;
        }
        else if(candidate->type == cgltf_attribute_type_normal) {
            if(normal) {
                errno = EILSEQ;
                return -1;
            }
            normal = candidate->data;
        }
        else {
            errno = ENOTSUP;
            return -1;
        }
    }
    if((position && (position->type != cgltf_type_vec3 ||
                     vertex >= position->count)) ||
       (normal && (normal->type != cgltf_type_vec3 ||
                   vertex >= normal->count))) {
        errno = EILSEQ;
        return -1;
    }
    if(position) {
        if(!cgltf_accessor_read_float(position, vertex, values, 3)) {
            errno = EILSEQ;
            return -1;
        }
        for(component = 0; component < 3; ++component) {
            if(!isfinite(values[component])) {
                errno = EILSEQ;
                return -1;
            }
        }
        delta->delta.position.x = values[0];
        delta->delta.position.y = values[1];
        delta->delta.position.z = values[2];
    }
    if(normal) {
        if(!cgltf_accessor_read_float(normal, vertex, values, 3)) {
            errno = EILSEQ;
            return -1;
        }
        for(component = 0; component < 3; ++component) {
            if(!isfinite(values[component])) {
                errno = EILSEQ;
                return -1;
            }
        }
        delta->delta.normal.x = values[0];
        delta->delta.normal.y = values[1];
        delta->delta.normal.z = values[2];
    }
    *nonzero = delta->delta.position.x != 0.0f ||
               delta->delta.position.y != 0.0f ||
               delta->delta.position.z != 0.0f ||
               delta->delta.normal.x != 0.0f ||
               delta->delta.normal.y != 0.0f ||
               delta->delta.normal.z != 0.0f;
    return 0;
}

static int gltf_build_shapes(const cgltf_mesh *mesh,
                             gltf_model_metadata_t *metadata) {
    size_t *counts = NULL;
    size_t target_count;
    size_t total_deltas = 0;
    size_t source_vertex;
    size_t output_index;
    cgltf_size primitive;
    size_t target_index;
    int saved_errno;

    if(!mesh->primitives_count)
        return 0;
    target_count = mesh->primitives[0].targets_count;
    if(!target_count)
        return 0;
    if(target_count > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    counts = calloc(target_count, sizeof(*counts));
    metadata->shape_targets = calloc(
        target_count, sizeof(*metadata->shape_targets));
    if(!counts || !metadata->shape_targets) {
        errno = ENOMEM;
        goto fail;
    }

    for(primitive = 0; primitive < mesh->primitives_count; ++primitive) {
        const cgltf_primitive *source = &mesh->primitives[primitive];
        const cgltf_accessor *positions = gltf_attribute(
            source, cgltf_attribute_type_position, 0);
        cgltf_size vertex;

        if(source->targets_count != target_count || !positions) {
            errno = EILSEQ;
            goto fail;
        }
        for(target_index = 0; target_index < target_count; ++target_index) {
            for(vertex = 0; vertex < positions->count; ++vertex) {
                pvr_chunk_shape_delta_t delta;
                int nonzero;

                if(gltf_target_delta(&source->targets[target_index], vertex,
                                     &delta, &nonzero) < 0)
                    goto fail;
                if(nonzero) {
                    if(counts[target_index] == SIZE_MAX ||
                       total_deltas == SIZE_MAX) {
                        errno = EOVERFLOW;
                        goto fail;
                    }
                    ++counts[target_index];
                    ++total_deltas;
                }
            }
        }
    }
    if(!total_deltas) {
        errno = EILSEQ;
        goto fail;
    }
    metadata->shape_deltas = calloc(
        total_deltas, sizeof(*metadata->shape_deltas));
    if(!metadata->shape_deltas) {
        errno = ENOMEM;
        goto fail;
    }

    output_index = 0;
    for(target_index = 0; target_index < target_count; ++target_index) {
        metadata->shape_targets[target_index].deltas =
            metadata->shape_deltas + output_index;
        metadata->shape_targets[target_index].delta_count =
            counts[target_index];
        source_vertex = 0;
        for(primitive = 0; primitive < mesh->primitives_count; ++primitive) {
            const cgltf_primitive *source = &mesh->primitives[primitive];
            const cgltf_accessor *positions = gltf_attribute(
                source, cgltf_attribute_type_position, 0);
            cgltf_size vertex;

            for(vertex = 0; vertex < positions->count; ++vertex) {
                pvr_chunk_shape_delta_t delta;
                int nonzero;

                if(gltf_target_delta(&source->targets[target_index], vertex,
                                     &delta, &nonzero) < 0)
                    goto fail;
                if(!nonzero)
                    continue;
                if(source_vertex + vertex > UINT16_MAX) {
                    errno = EOVERFLOW;
                    goto fail;
                }
                delta.vertex_index = (uint16_t)(source_vertex + vertex);
                metadata->shape_deltas[output_index++] = delta;
            }
            source_vertex += positions->count;
        }
    }
    if(output_index != total_deltas) {
        errno = EPROTO;
        goto fail;
    }
    metadata->shapes.targets = metadata->shape_targets;
    metadata->shapes.target_count = target_count;
    free(counts);
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    free(counts);
    errno = saved_errno;
    return -1;
}

static int gltf_matrix_fallback(const float matrix[16],
                                anim_transform_t *fallback) {
    /* PCH1 retains an arbitrary local matrix, but PAT1 evaluates TRS. Accept
       a static matrix in an animated scene only when its column axes are
       orthogonal, preserve a reflection as signed X scale, and rebuild the
       matrix before publishing the fallback. */
    double axis[3][3];
    double scale[3];
    double rotation[9];
    double quaternion[4];
    double determinant;
    double magnitude;
    double value;
    size_t column;
    size_t row;
    size_t component;

    for(component = 0; component < 16; ++component) {
        if(!isfinite(matrix[component])) {
            errno = EILSEQ;
            return -1;
        }
    }
    if(fabs((double)matrix[3]) > 64.0 * FLT_EPSILON ||
       fabs((double)matrix[7]) > 64.0 * FLT_EPSILON ||
       fabs((double)matrix[11]) > 64.0 * FLT_EPSILON ||
       fabs((double)matrix[15] - 1.0) > 64.0 * FLT_EPSILON) {
        errno = ENOTSUP;
        return -1;
    }
    for(column = 0; column < 3; ++column) {
        magnitude = 0.0;
        for(row = 0; row < 3; ++row) {
            value = matrix[column * 4u + row];
            magnitude += value * value;
        }
        scale[column] = sqrt(magnitude);
        if(!(scale[column] > FLT_MIN) || !isfinite(scale[column])) {
            errno = ENOTSUP;
            return -1;
        }
        for(row = 0; row < 3; ++row)
            axis[column][row] =
                matrix[column * 4u + row] / scale[column];
    }
    for(column = 0; column < 3; ++column) {
        size_t other;

        for(other = column + 1u; other < 3; ++other) {
            double dot = axis[column][0] * axis[other][0] +
                         axis[column][1] * axis[other][1] +
                         axis[column][2] * axis[other][2];

            if(fabs(dot) > 128.0 * FLT_EPSILON) {
                errno = ENOTSUP;
                return -1;
            }
        }
    }
    determinant =
        axis[0][0] * (axis[1][1] * axis[2][2] -
                      axis[1][2] * axis[2][1]) -
        axis[1][0] * (axis[0][1] * axis[2][2] -
                      axis[0][2] * axis[2][1]) +
        axis[2][0] * (axis[0][1] * axis[1][2] -
                      axis[0][2] * axis[1][1]);
    if(determinant < 0.0) {
        scale[0] = -scale[0];
        for(row = 0; row < 3; ++row)
            axis[0][row] = -axis[0][row];
        determinant = -determinant;
    }
    if(fabs(determinant - 1.0) > 256.0 * FLT_EPSILON) {
        errno = ENOTSUP;
        return -1;
    }

    value = axis[0][0] + axis[1][1] + axis[2][2];
    if(value > 0.0) {
        magnitude = sqrt(value + 1.0) * 2.0;
        quaternion[0] = 0.25 * magnitude;
        quaternion[1] = (axis[1][2] - axis[2][1]) / magnitude;
        quaternion[2] = (axis[2][0] - axis[0][2]) / magnitude;
        quaternion[3] = (axis[0][1] - axis[1][0]) / magnitude;
    }
    else if(axis[0][0] > axis[1][1] &&
            axis[0][0] > axis[2][2]) {
        magnitude = sqrt(1.0 + axis[0][0] - axis[1][1] -
                         axis[2][2]) * 2.0;
        quaternion[0] = (axis[1][2] - axis[2][1]) / magnitude;
        quaternion[1] = 0.25 * magnitude;
        quaternion[2] = (axis[1][0] + axis[0][1]) / magnitude;
        quaternion[3] = (axis[2][0] + axis[0][2]) / magnitude;
    }
    else if(axis[1][1] > axis[2][2]) {
        magnitude = sqrt(1.0 + axis[1][1] - axis[0][0] -
                         axis[2][2]) * 2.0;
        quaternion[0] = (axis[2][0] - axis[0][2]) / magnitude;
        quaternion[1] = (axis[1][0] + axis[0][1]) / magnitude;
        quaternion[2] = 0.25 * magnitude;
        quaternion[3] = (axis[2][1] + axis[1][2]) / magnitude;
    }
    else {
        magnitude = sqrt(1.0 + axis[2][2] - axis[0][0] -
                         axis[1][1]) * 2.0;
        quaternion[0] = (axis[0][1] - axis[1][0]) / magnitude;
        quaternion[1] = (axis[2][0] + axis[0][2]) / magnitude;
        quaternion[2] = (axis[2][1] + axis[1][2]) / magnitude;
        quaternion[3] = 0.25 * magnitude;
    }
    magnitude = sqrt(quaternion[0] * quaternion[0] +
                     quaternion[1] * quaternion[1] +
                     quaternion[2] * quaternion[2] +
                     quaternion[3] * quaternion[3]);
    if(!(magnitude > 0.0) || !isfinite(magnitude)) {
        errno = EILSEQ;
        return -1;
    }
    for(component = 0; component < 4; ++component)
        quaternion[component] /= magnitude;

    rotation[0] = 1.0 - 2.0 *
        (quaternion[2] * quaternion[2] +
         quaternion[3] * quaternion[3]);
    rotation[1] = 2.0 *
        (quaternion[1] * quaternion[2] +
         quaternion[3] * quaternion[0]);
    rotation[2] = 2.0 *
        (quaternion[1] * quaternion[3] -
         quaternion[2] * quaternion[0]);
    rotation[3] = 2.0 *
        (quaternion[1] * quaternion[2] -
         quaternion[3] * quaternion[0]);
    rotation[4] = 1.0 - 2.0 *
        (quaternion[1] * quaternion[1] +
         quaternion[3] * quaternion[3]);
    rotation[5] = 2.0 *
        (quaternion[2] * quaternion[3] +
         quaternion[1] * quaternion[0]);
    rotation[6] = 2.0 *
        (quaternion[1] * quaternion[3] +
         quaternion[2] * quaternion[0]);
    rotation[7] = 2.0 *
        (quaternion[2] * quaternion[3] -
         quaternion[1] * quaternion[0]);
    rotation[8] = 1.0 - 2.0 *
        (quaternion[1] * quaternion[1] +
         quaternion[2] * quaternion[2]);
    for(column = 0; column < 3; ++column) {
        for(row = 0; row < 3; ++row) {
            double source = matrix[column * 4u + row];
            double rebuilt = rotation[column * 3u + row] * scale[column];
            double tolerance = 256.0 * FLT_EPSILON *
                               fmax(1.0, fabs(source));

            if(fabs(rebuilt - source) > tolerance) {
                errno = ENOTSUP;
                return -1;
            }
        }
    }

    fallback->translation.x = matrix[12];
    fallback->translation.y = matrix[13];
    fallback->translation.z = matrix[14];
    fallback->translation.w = 1.0f;
    fallback->rotation.w = (float)quaternion[0];
    fallback->rotation.x = (float)quaternion[1];
    fallback->rotation.y = (float)quaternion[2];
    fallback->rotation.z = (float)quaternion[3];
    fallback->scale.x = (float)scale[0];
    fallback->scale.y = (float)scale[1];
    fallback->scale.z = (float)scale[2];
    fallback->scale.w = 0.0f;
    return 0;
}

static int gltf_animation_fallbacks(
    const cgltf_data *data, const size_t *node_to_scene,
    const pvr_scene_ir_t *scene, gltf_animation_metadata_t *metadata) {
    size_t node;

    for(node = 0; node < data->nodes_count; ++node) {
        const cgltf_node *source = &data->nodes[node];
        anim_transform_tracks_t *transform;
        anim_visibility_tracks_t *visibility;
        size_t scene_index = node_to_scene[node];
        double length_squared;
        double inverse_length;

        if(scene_index == SIZE_MAX)
            continue;
        if(scene_index >= scene->node_count) {
            errno = EILSEQ;
            return -1;
        }
        transform = &metadata->animation_transforms[scene_index];
        visibility = &metadata->animation_visibility[scene_index];
        if(source->has_matrix) {
            if(gltf_matrix_fallback(source->matrix,
                                    &transform->fallback) < 0)
                return -1;
            visibility->fallback = true;
            continue;
        }
        transform->fallback.translation.x = source->has_translation ?
            source->translation[0] : 0.0f;
        transform->fallback.translation.y = source->has_translation ?
            source->translation[1] : 0.0f;
        transform->fallback.translation.z = source->has_translation ?
            source->translation[2] : 0.0f;
        transform->fallback.translation.w = 1.0f;
        transform->fallback.rotation.w = source->has_rotation ?
            source->rotation[3] : 1.0f;
        transform->fallback.rotation.x = source->has_rotation ?
            source->rotation[0] : 0.0f;
        transform->fallback.rotation.y = source->has_rotation ?
            source->rotation[1] : 0.0f;
        transform->fallback.rotation.z = source->has_rotation ?
            source->rotation[2] : 0.0f;
        length_squared =
            (double)transform->fallback.rotation.w *
                transform->fallback.rotation.w +
            (double)transform->fallback.rotation.x *
                transform->fallback.rotation.x +
            (double)transform->fallback.rotation.y *
                transform->fallback.rotation.y +
            (double)transform->fallback.rotation.z *
                transform->fallback.rotation.z;
        if(!(length_squared > 0.0) || !isfinite(length_squared)) {
            errno = EILSEQ;
            return -1;
        }
        inverse_length = 1.0 / sqrt(length_squared);
        transform->fallback.rotation.w *= (float)inverse_length;
        transform->fallback.rotation.x *= (float)inverse_length;
        transform->fallback.rotation.y *= (float)inverse_length;
        transform->fallback.rotation.z *= (float)inverse_length;
        transform->fallback.scale.x = source->has_scale ?
            source->scale[0] : 1.0f;
        transform->fallback.scale.y = source->has_scale ?
            source->scale[1] : 1.0f;
        transform->fallback.scale.z = source->has_scale ?
            source->scale[2] : 1.0f;
        visibility->fallback = true;
    }
    return 0;
}

static int gltf_build_animation(const cgltf_data *data,
                                const cgltf_animation *animation,
                                const size_t *node_to_scene,
                                const pvr_scene_ir_t *scene,
                                const gltf_asset_metadata_t *asset_metadata,
                                gltf_animation_metadata_t *metadata) {
    size_t vector_key_count = 0;
    size_t quaternion_key_count = 0;
    size_t scalar_key_count = 0;
    size_t transform_channel_count = 0;
    size_t morph_binding_count = 0;
    size_t morph_channel_count = 0;
    size_t vector_cursor = 0;
    size_t quaternion_cursor = 0;
    size_t scalar_cursor = 0;
    size_t transform_cursor = 0;
    size_t morph_binding_cursor = 0;
    size_t morph_channel_cursor = 0;
    float transform_start = FLT_MAX;
    float transform_end = -FLT_MAX;
    float morph_start = FLT_MAX;
    float morph_end = -FLT_MAX;
    cgltf_size channel;

    if(!data || !animation || !node_to_scene || !scene ||
       !asset_metadata || !metadata || !scene->node_count ||
       !animation->channels_count) {
        errno = EILSEQ;
        return -1;
    }
    for(channel = 0; channel < animation->channels_count; ++channel) {
        const cgltf_animation_channel *source =
            &animation->channels[channel];

        if(!source->sampler || !source->sampler->input ||
           !source->sampler->output || !source->target_node ||
           (source->sampler->interpolation !=
                cgltf_interpolation_type_step &&
            source->sampler->interpolation !=
                cgltf_interpolation_type_linear &&
            source->sampler->interpolation !=
                cgltf_interpolation_type_cubic_spline) ||
           source->sampler->input->type != cgltf_type_scalar ||
           !source->sampler->input->count) {
            errno = ENOTSUP;
            return -1;
        }
        if(source->target_path != cgltf_animation_path_type_weights &&
           source->target_node->has_matrix) {
            errno = ENOTSUP;
            return -1;
        }
        if(source->target_path == cgltf_animation_path_type_weights) {
            const cgltf_node *target = source->target_node;
            size_t target_count;
            size_t output_factor = source->sampler->interpolation ==
                                       cgltf_interpolation_type_cubic_spline ?
                                       3u : 1u;
            size_t node_ordinal;
            size_t scene_index;
            uint32_t model_ordinal;
            cgltf_size previous;

            if(!target->mesh || !target->mesh->primitives_count) {
                errno = EILSEQ;
                return -1;
            }
            target_count = target->mesh->primitives[0].targets_count;
            if(!target_count || source->sampler->output->type !=
                                    cgltf_type_scalar ||
               target_count > SIZE_MAX /
                                  source->sampler->input->count /
                                  output_factor ||
               source->sampler->output->count !=
                   source->sampler->input->count * target_count *
                       output_factor ||
               gltf_array_index(data->nodes, data->nodes_count,
                                sizeof(*data->nodes), target,
                                &node_ordinal) < 0) {
                errno = EILSEQ;
                return -1;
            }
            scene_index = node_to_scene[node_ordinal];
            if(scene_index == SIZE_MAX || scene_index >= scene->node_count) {
                errno = ENOTSUP;
                return -1;
            }
            model_ordinal = scene->nodes[scene_index].model_ordinal;
            if(model_ordinal == PVR_CHUNK_SCENE_MODEL_NONE ||
               model_ordinal >= asset_metadata->model_count ||
               asset_metadata->models[model_ordinal].shapes.target_count !=
                   target_count) {
                errno = EILSEQ;
                return -1;
            }
            for(previous = 0; previous < channel; ++previous) {
                if(animation->channels[previous].target_path ==
                       cgltf_animation_path_type_weights &&
                   animation->channels[previous].target_node == target) {
                    errno = EILSEQ;
                    return -1;
                }
            }
            if(target_count > UINT32_MAX - morph_channel_count ||
               source->sampler->input->count >
                   (SIZE_MAX - scalar_key_count) / target_count ||
               morph_binding_count == UINT32_MAX) {
                errno = EOVERFLOW;
                return -1;
            }
            morph_channel_count += target_count;
            scalar_key_count += source->sampler->input->count *
                                target_count;
            ++morph_binding_count;
        }
        else if(source->sampler->input->count > SIZE_MAX /
                    (source->sampler->interpolation ==
                         cgltf_interpolation_type_cubic_spline ? 3u : 1u) ||
                source->sampler->output->count !=
                    source->sampler->input->count *
                        (source->sampler->interpolation ==
                             cgltf_interpolation_type_cubic_spline ?
                             3u : 1u)) {
            errno = EILSEQ;
            return -1;
        }
        else if(source->target_path == cgltf_animation_path_type_rotation) {
            if(source->sampler->output->type != cgltf_type_vec4 ||
               source->sampler->input->count >
                   SIZE_MAX - quaternion_key_count) {
                errno = EILSEQ;
                return -1;
            }
            quaternion_key_count += source->sampler->input->count;
        }
        else if(source->target_path ==
                    cgltf_animation_path_type_translation ||
                source->target_path == cgltf_animation_path_type_scale) {
            if(source->sampler->output->type != cgltf_type_vec3 ||
               source->sampler->input->count >
                   SIZE_MAX - vector_key_count) {
                errno = EILSEQ;
                return -1;
            }
            vector_key_count += source->sampler->input->count;
        }
        else {
            errno = ENOTSUP;
            return -1;
        }
        if(source->target_path != cgltf_animation_path_type_weights)
            ++transform_channel_count;
    }
    if(transform_channel_count) {
        metadata->animation_vector_keys = calloc(
            vector_key_count, sizeof(*metadata->animation_vector_keys));
        metadata->animation_quaternion_keys = calloc(
            quaternion_key_count,
            sizeof(*metadata->animation_quaternion_keys));
        metadata->animation_tracks = calloc(
            transform_channel_count, sizeof(*metadata->animation_tracks));
        metadata->animation_transforms = calloc(
            scene->node_count, sizeof(*metadata->animation_transforms));
        metadata->animation_visibility = calloc(
            scene->node_count, sizeof(*metadata->animation_visibility));
    }
    if(morph_binding_count) {
        metadata->morph_animation_keys = calloc(
            scalar_key_count, sizeof(*metadata->morph_animation_keys));
        metadata->morph_animation_tracks = calloc(
            morph_channel_count,
            sizeof(*metadata->morph_animation_tracks));
        metadata->morph_animation_channels = calloc(
            morph_channel_count,
            sizeof(*metadata->morph_animation_channels));
        metadata->morph_animation_bindings = calloc(
            morph_binding_count,
            sizeof(*metadata->morph_animation_bindings));
    }
    if((vector_key_count && !metadata->animation_vector_keys) ||
       (quaternion_key_count && !metadata->animation_quaternion_keys) ||
       (transform_channel_count &&
        (!metadata->animation_tracks || !metadata->animation_transforms ||
         !metadata->animation_visibility)) ||
       (morph_binding_count &&
        (!metadata->morph_animation_keys ||
         !metadata->morph_animation_tracks ||
         !metadata->morph_animation_channels ||
         !metadata->morph_animation_bindings))) {
        errno = ENOMEM;
        return -1;
    }
    if(transform_channel_count &&
       gltf_animation_fallbacks(data, node_to_scene, scene, metadata) < 0)
        return -1;

    for(channel = 0; channel < animation->channels_count; ++channel) {
        const cgltf_animation_channel *source =
            &animation->channels[channel];
        const cgltf_node *target = source->target_node;
        anim_track_view_t *track;
        anim_transform_tracks_t *transform;
        size_t node_ordinal;
        size_t scene_index;
        size_t key;
        float previous_time = -FLT_MAX;

        if(gltf_array_index(data->nodes, data->nodes_count,
                            sizeof(*data->nodes), target,
                            &node_ordinal) < 0)
            return -1;
        scene_index = node_to_scene[node_ordinal];
        if(scene_index == SIZE_MAX) {
            errno = ENOTSUP;
            return -1;
        }
        if(source->target_path == cgltf_animation_path_type_weights) {
            const cgltf_mesh *mesh = target->mesh;
            size_t target_count = mesh->primitives[0].targets_count;
            pvr_chunk_morph_animation_binding_t *binding =
                metadata->morph_animation_bindings + morph_binding_cursor;
            const float *fallbacks = NULL;
            size_t fallback_count = 0;
            size_t target_index;

            if(target->weights_count) {
                fallbacks = target->weights;
                fallback_count = target->weights_count;
            }
            else if(mesh->weights_count) {
                fallbacks = mesh->weights;
                fallback_count = mesh->weights_count;
            }
            if(fallback_count && fallback_count != target_count) {
                errno = EILSEQ;
                return -1;
            }
            binding->node_index = scene_index;
            binding->model_ordinal = scene->nodes[scene_index].model_ordinal;
            binding->channels = metadata->morph_animation_channels +
                                morph_channel_cursor;
            binding->channel_count = target_count;
            for(target_index = 0; target_index < target_count;
                ++target_index) {
                pvr_chunk_shape_channel_t *destination_channel =
                    metadata->morph_animation_channels +
                    morph_channel_cursor + target_index;
                anim_track_view_t *destination_track =
                    metadata->morph_animation_tracks +
                    morph_channel_cursor + target_index;
                anim_scalar_hermite_key_t *destination_keys =
                    metadata->morph_animation_keys + scalar_cursor;

                destination_channel->fallback_weight = fallbacks ?
                    fallbacks[target_index] : 0.0f;
                if(!isfinite(destination_channel->fallback_weight)) {
                    errno = EILSEQ;
                    return -1;
                }
                destination_channel->weight = destination_track;
                destination_track->track.kind = ANIM_VALUE_SCALAR;
                destination_track->track.interpolation =
                    source->sampler->interpolation ==
                        cgltf_interpolation_type_step ?
                        ANIM_INTERPOLATION_STEP :
                    source->sampler->interpolation ==
                        cgltf_interpolation_type_cubic_spline ?
                        ANIM_INTERPOLATION_CUBIC_HERMITE :
                        ANIM_INTERPOLATION_LINEAR;
                destination_track->track.keys = destination_keys;
                destination_track->track.key_count =
                    source->sampler->input->count;
                destination_track->track.stride =
                    sizeof(*destination_keys);
                previous_time = -FLT_MAX;
                for(key = 0; key < source->sampler->input->count; ++key) {
                    float time;
                    float values[3] = { 0.0f, 0.0f, 0.0f };
                    size_t output = key * target_count + target_index;
                    int cubic = source->sampler->interpolation ==
                                cgltf_interpolation_type_cubic_spline;

                    if(!cgltf_accessor_read_float(source->sampler->input,
                                                  key, &time, 1) ||
                       !cgltf_accessor_read_float(
                           source->sampler->output,
                           cubic ? (key * 3u) * target_count + target_index :
                                   output,
                           &values[0], 1) ||
                       (cubic &&
                        (!cgltf_accessor_read_float(
                             source->sampler->output,
                             (key * 3u + 1u) * target_count + target_index,
                             &values[1], 1) ||
                         !cgltf_accessor_read_float(
                             source->sampler->output,
                             (key * 3u + 2u) * target_count + target_index,
                             &values[2], 1))) ||
                       !isfinite(time) || !isfinite(values[0]) ||
                       (cubic && (!isfinite(values[1]) ||
                                  !isfinite(values[2]))) ||
                       (key && time <= previous_time)) {
                        errno = EILSEQ;
                        return -1;
                    }
                    destination_keys[key].time = time;
                    destination_keys[key].value = cubic ? values[1] :
                                                         values[0];
                    destination_keys[key].in_tangent = cubic ? values[0] :
                                                               0.0f;
                    destination_keys[key].out_tangent = cubic ? values[2] :
                                                                0.0f;
                    previous_time = time;
                }
                destination_track->start_time = destination_keys[0].time;
                destination_track->end_time =
                    destination_keys[source->sampler->input->count - 1u]
                        .time;
                if(destination_track->start_time < morph_start)
                    morph_start = destination_track->start_time;
                if(destination_track->end_time > morph_end)
                    morph_end = destination_track->end_time;
                scalar_cursor += source->sampler->input->count;
            }
            morph_channel_cursor += target_count;
            ++morph_binding_cursor;
            continue;
        }
        track = &metadata->animation_tracks[transform_cursor++];
        transform = &metadata->animation_transforms[scene_index];
        track->track.interpolation =
            source->sampler->interpolation ==
                cgltf_interpolation_type_step ?
                ANIM_INTERPOLATION_STEP :
            source->sampler->interpolation ==
                cgltf_interpolation_type_cubic_spline ?
                ANIM_INTERPOLATION_CUBIC_HERMITE :
                ANIM_INTERPOLATION_LINEAR;
        if(source->target_path == cgltf_animation_path_type_rotation) {
            if(transform->rotation) {
                errno = EILSEQ;
                return -1;
            }
            track->track.kind = ANIM_VALUE_QUATERNION;
            track->track.keys = metadata->animation_quaternion_keys +
                                quaternion_cursor;
            track->track.stride = sizeof(anim_quaternion_hermite_key_t);
            transform->rotation = track;
        }
        else {
            if((source->target_path ==
                    cgltf_animation_path_type_translation &&
                transform->translation) ||
               (source->target_path == cgltf_animation_path_type_scale &&
                transform->scale)) {
                errno = EILSEQ;
                return -1;
            }
            track->track.kind = ANIM_VALUE_VECTOR;
            track->track.keys = metadata->animation_vector_keys +
                                vector_cursor;
            track->track.stride = sizeof(anim_vector_hermite_key_t);
            if(source->target_path ==
               cgltf_animation_path_type_translation)
                transform->translation = track;
            else
                transform->scale = track;
        }
        track->track.key_count = source->sampler->input->count;

        for(key = 0; key < track->track.key_count; ++key) {
            float time;

            if(!cgltf_accessor_read_float(source->sampler->input, key,
                                          &time, 1) ||
               !isfinite(time) || (key && time <= previous_time)) {
                errno = EILSEQ;
                return -1;
            }
            previous_time = time;
            if(source->target_path ==
               cgltf_animation_path_type_rotation) {
                anim_quaternion_hermite_key_t *destination =
                    &metadata->animation_quaternion_keys[
                        quaternion_cursor + key];
                float sample[3][4] = { { 0 } };
                size_t value_index =
                    source->sampler->interpolation ==
                        cgltf_interpolation_type_cubic_spline ?
                        key * 3u + 1u : key;
                int cubic = source->sampler->interpolation ==
                            cgltf_interpolation_type_cubic_spline;
                double length_squared;
                double inverse_length;

                if(!cgltf_accessor_read_float(source->sampler->output,
                                              value_index, sample[1], 4) ||
                   (cubic &&
                    (!cgltf_accessor_read_float(source->sampler->output,
                                                key * 3u, sample[0], 4) ||
                     !cgltf_accessor_read_float(source->sampler->output,
                                                key * 3u + 2u,
                                                sample[2], 4)))) {
                    errno = EILSEQ;
                    return -1;
                }
                length_squared = (double)sample[1][0] * sample[1][0] +
                                 (double)sample[1][1] * sample[1][1] +
                                 (double)sample[1][2] * sample[1][2] +
                                 (double)sample[1][3] * sample[1][3];
                if(!(length_squared > 0.0) ||
                   !isfinite(length_squared)) {
                    errno = EILSEQ;
                    return -1;
                }
                inverse_length = 1.0 / sqrt(length_squared);
                destination->time = time;
                destination->value.w = sample[1][3] * (float)inverse_length;
                destination->value.x = sample[1][0] * (float)inverse_length;
                destination->value.y = sample[1][1] * (float)inverse_length;
                destination->value.z = sample[1][2] * (float)inverse_length;
                if(cubic) {
                    destination->in_tangent = (anim_quaternion_t) {
                        sample[0][3], sample[0][0], sample[0][1],
                        sample[0][2]
                    };
                    destination->out_tangent = (anim_quaternion_t) {
                        sample[2][3], sample[2][0], sample[2][1],
                        sample[2][2]
                    };
                }
            }
            else {
                anim_vector_hermite_key_t *destination =
                    &metadata->animation_vector_keys[vector_cursor + key];
                float sample[3][3] = { { 0 } };
                size_t value_index =
                    source->sampler->interpolation ==
                        cgltf_interpolation_type_cubic_spline ?
                        key * 3u + 1u : key;
                int cubic = source->sampler->interpolation ==
                            cgltf_interpolation_type_cubic_spline;

                if(!cgltf_accessor_read_float(source->sampler->output,
                                              value_index, sample[1], 3) ||
                   (cubic &&
                    (!cgltf_accessor_read_float(source->sampler->output,
                                                key * 3u, sample[0], 3) ||
                     !cgltf_accessor_read_float(source->sampler->output,
                                                key * 3u + 2u,
                                                sample[2], 3))) ||
                   !isfinite(sample[1][0]) || !isfinite(sample[1][1]) ||
                   !isfinite(sample[1][2]) ||
                   (cubic &&
                    (!isfinite(sample[0][0]) || !isfinite(sample[0][1]) ||
                     !isfinite(sample[0][2]) || !isfinite(sample[2][0]) ||
                     !isfinite(sample[2][1]) ||
                     !isfinite(sample[2][2])))) {
                    errno = EILSEQ;
                    return -1;
                }
                destination->time = time;
                destination->value.x = sample[1][0];
                destination->value.y = sample[1][1];
                destination->value.z = sample[1][2];
                destination->value.w =
                    source->target_path ==
                        cgltf_animation_path_type_translation ? 1.0f : 0.0f;
                if(cubic) {
                    destination->in_tangent = (vector_t) {
                        sample[0][0], sample[0][1], sample[0][2], 0.0f
                    };
                    destination->out_tangent = (vector_t) {
                        sample[2][0], sample[2][1], sample[2][2], 0.0f
                    };
                }
            }
        }
        if(source->target_path == cgltf_animation_path_type_rotation) {
            const anim_quaternion_hermite_key_t *keys = track->track.keys;

            track->start_time = keys[0].time;
            track->end_time = keys[track->track.key_count - 1u].time;
        }
        else {
            const anim_vector_hermite_key_t *keys = track->track.keys;

            track->start_time = keys[0].time;
            track->end_time = keys[track->track.key_count - 1u].time;
        }
        if(track->start_time < transform_start)
            transform_start = track->start_time;
        if(track->end_time > transform_end)
            transform_end = track->end_time;
        if(source->target_path == cgltf_animation_path_type_rotation)
            quaternion_cursor += track->track.key_count;
        else
            vector_cursor += track->track.key_count;
    }
    if(vector_cursor != vector_key_count ||
       quaternion_cursor != quaternion_key_count ||
       scalar_cursor != scalar_key_count ||
       transform_cursor != transform_channel_count ||
       morph_binding_cursor != morph_binding_count ||
       morph_channel_cursor != morph_channel_count ||
       (transform_channel_count && transform_start >= transform_end) ||
       (morph_binding_count && morph_start >= morph_end)) {
        errno = EPROTO;
        return -1;
    }
    if(transform_channel_count) {
        metadata->animation.clip.transforms =
            metadata->animation_transforms;
        metadata->animation.clip.transform_count = scene->node_count;
        metadata->animation.clip.start_time = transform_start;
        metadata->animation.clip.end_time = transform_end;
        metadata->animation.clip.visibility =
            metadata->animation_visibility;
        metadata->animation_track_count = transform_channel_count;
        metadata->animation_key_count =
            vector_key_count + quaternion_key_count;
    }
    if(morph_binding_count) {
        size_t left;

        /* PMW1 orders bindings by hierarchy node, while glTF channel order is
           intentionally irrelevant. Each binding owns a stable channel span,
           so sorting the small descriptor array does not move track data. */
        for(left = 1; left < morph_binding_count; ++left) {
            pvr_chunk_morph_animation_binding_t value =
                metadata->morph_animation_bindings[left];
            size_t right = left;

            while(right && metadata->morph_animation_bindings[right - 1u]
                                .node_index > value.node_index) {
                metadata->morph_animation_bindings[right] =
                    metadata->morph_animation_bindings[right - 1u];
                --right;
            }
            metadata->morph_animation_bindings[right] = value;
        }
        metadata->morph_animation.bindings =
            metadata->morph_animation_bindings;
        metadata->morph_animation.binding_count = morph_binding_count;
        metadata->morph_animation.start_time = morph_start;
        metadata->morph_animation.end_time = morph_end;
        metadata->morph_animation_track_count = morph_channel_count;
        metadata->morph_animation_key_count = scalar_key_count;
    }
    return 0;
}

static int gltf_build_animations(const cgltf_data *data,
                                 const size_t *node_to_scene,
                                 const pvr_scene_ir_t *scene,
                                 gltf_asset_metadata_t *metadata) {
    size_t animation;

    if(!data->animations_count)
        return 0;
    if(!scene->node_count) {
        errno = ENOTSUP;
        return -1;
    }
    if(data->animations_count > UINT32_MAX ||
       data->animations_count > SIZE_MAX /
           sizeof(*metadata->animations)) {
        errno = EOVERFLOW;
        return -1;
    }
    metadata->animations = calloc(data->animations_count,
                                  sizeof(*metadata->animations));
    if(!metadata->animations) {
        errno = ENOMEM;
        return -1;
    }
    metadata->animation_count = data->animations_count;
    for(animation = 0; animation < metadata->animation_count; ++animation) {
        const cgltf_animation *source = &data->animations[animation];
        size_t matching_names = 0;
        size_t candidate;

        if(gltf_build_animation(data, source, node_to_scene, scene,
                                metadata,
                                &metadata->animations[animation]) < 0)
            return -1;
        if(!source->name || !source->name[0])
            continue;
        for(candidate = 0; candidate < data->animations_count; ++candidate) {
            const char *name = data->animations[candidate].name;

            if(name && !strcmp(name, source->name))
                ++matching_names;
        }
        /* A catalog name is an exact unique lookup key. Legal duplicate glTF
           names remain addressable by source order instead of being changed. */
        if(matching_names == 1) {
            size_t name_bytes = strlen(source->name) + 1u;

            metadata->animations[animation].name = malloc(name_bytes);
            if(!metadata->animations[animation].name) {
                errno = ENOMEM;
                return -1;
            }
            memcpy(metadata->animations[animation].name, source->name,
                   name_bytes);
        }
    }
    return 0;
}

static int load_gltf_source(const char *path, source_model_set_t *models,
                            int flip_winding, int flip_v,
                            int texture_identifier,
                            material_library_t *library,
                            pvr_scene_ir_t *scene,
                            gltf_asset_metadata_t *metadata) {
    cgltf_options options = { 0 };
    cgltf_data *data = NULL;
    const cgltf_scene *source_scene;
    const cgltf_mesh **meshes = NULL;
    const cgltf_skin **skins = NULL;
    size_t *node_to_scene = NULL;
    size_t mesh_count = 0;
    cgltf_result result;
    size_t model;
    cgltf_size primitive;
    int saved_errno;

    memset(models, 0, sizeof(*models));

    result = cgltf_parse_file(&options, path, &data);
    if(result != cgltf_result_success) {
        errno = cgltf_errno(result);
        return -1;
    }
    result = cgltf_load_buffers(&options, data, path);
    if(result != cgltf_result_success) {
        errno = cgltf_errno(result);
        goto fail;
    }
    result = cgltf_validate(data);
    if(result != cgltf_result_success) {
        errno = cgltf_errno(result);
        goto fail;
    }
    if(gltf_required_extensions_supported(data) < 0)
        goto fail;
    if(!data->scenes_count || data->nodes_count > 65536u) {
        errno = ENOTSUP;
        goto fail;
    }
    node_to_scene = malloc(data->nodes_count * sizeof(*node_to_scene));
    meshes = calloc(data->meshes_count, sizeof(*meshes));
    skins = calloc(data->meshes_count, sizeof(*skins));
    if(!node_to_scene || !meshes || !skins) {
        errno = ENOMEM;
        goto fail;
    }
    source_scene = data->scene ? data->scene : &data->scenes[0];
    if(gltf_build_scene(data, source_scene, meshes, skins, &mesh_count,
                        scene, node_to_scene) < 0 ||
       gltf_add_materials(data, library) < 0)
        goto fail;
    models->models = calloc(mesh_count, sizeof(*models->models));
    metadata->models = calloc(mesh_count, sizeof(*metadata->models));
    if(!models->models || !metadata->models) {
        errno = ENOMEM;
        goto fail;
    }
    models->count = mesh_count;
    metadata->model_count = mesh_count;
    for(model = 0; model < mesh_count; ++model) {
        const cgltf_mesh *mesh = meshes[model];

        /* A model uses one vertex-record family. Scan first so an uncolored
           primitive preceding a colored one receives the material base color
           instead of being emitted without a color attribute. */
        for(primitive = 0; primitive < mesh->primitives_count; ++primitive) {
            if(gltf_attribute(&mesh->primitives[primitive],
                              cgltf_attribute_type_color, 0)) {
                models->models[model].has_colors = 1;
                break;
            }
        }
        for(primitive = 0; primitive < mesh->primitives_count;
            ++primitive) {
            if(gltf_append_primitive(
                   data, &mesh->primitives[primitive],
                   &models->models[model], flip_winding, flip_v,
                   texture_identifier, skins[model] != NULL) < 0)
                goto fail;
        }
        if(validate_references(&models->models[model]) < 0 ||
           gltf_build_skin(data, mesh, skins[model], node_to_scene, scene,
                           &metadata->models[model]) < 0 ||
           gltf_build_shapes(mesh, &metadata->models[model]) < 0)
            goto fail;
    }
    if(gltf_build_animations(data, node_to_scene, scene, metadata) < 0 ||
       serialize_gltf_textures(
           data, path, texture_identifier, &metadata->texture_section,
           &metadata->texture_section_bytes) < 0)
        goto fail;
    free(skins);
    free(meshes);
    free(node_to_scene);
    cgltf_free(data);
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    source_model_set_free(models);
    free(skins);
    free(meshes);
    free(node_to_scene);
    cgltf_free(data);
    errno = saved_errno;
    return -1;
}

static int parse_index(const char *text, size_t current_count,
                       size_t *result) {
    char *end;
    intmax_t value;

    if(!*text) {
        errno = EILSEQ;
        return -1;
    }
    errno = 0;
    value = strtoimax(text, &end, 10);
    if(errno == ERANGE || end == text || *end || !value) {
        errno = EILSEQ;
        return -1;
    }

    if(value > 0) {
        /* Positive references may point forward and are checked after parse. */
        uintmax_t index = (uintmax_t)value - 1u;

        if(index > SIZE_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        *result = (size_t)index;
    }
    else {
        /* Relative references are anchored at this source line by
           definition. */
        uintmax_t magnitude = (uintmax_t)(-(value + 1)) + 1u;

        if(magnitude > current_count) {
            errno = EILSEQ;
            return -1;
        }
        *result = current_count - (size_t)magnitude;
    }
    return 0;
}

static int parse_corner(char *token, const source_model_t *model,
                        source_corner_t *corner) {
    char *first_slash = strchr(token, '/');
    char *second_slash = NULL;

    corner->texcoord = SIZE_MAX;
    corner->normal = SIZE_MAX;
    if(first_slash) {
        *first_slash = '\0';
        second_slash = strchr(first_slash + 1u, '/');
        if(second_slash) {
            *second_slash = '\0';
            if(strchr(second_slash + 1u, '/')) {
                errno = EILSEQ;
                return -1;
            }
        }
    }

    if(parse_index(token, model->position_count, &corner->position) < 0)
        return -1;
    if(first_slash && first_slash[1]) {
        if(parse_index(first_slash + 1u, model->texcoord_count,
                       &corner->texcoord) < 0)
            return -1;
    }
    if(second_slash) {
        if(!second_slash[1] ||
           parse_index(second_slash + 1u, model->normal_count,
                       &corner->normal) < 0)
            return -1;
    }
    else if(first_slash && !first_slash[1]) {
        errno = EILSEQ;
        return -1;
    }

    return 0;
}

static int append_position(source_model_t *model, char *cursor) {
    source_position_t position;
    void *allocation = model->positions;
    size_t component;

    if(model->position_count == MAX_POSITION_COUNT) {
        errno = EOVERFLOW;
        return -1;
    }
    for(component = 0; component < 3u; ++component) {
        char *token = next_token(&cursor);

        if(!token || parse_float_token(token, &position.value[component]) < 0)
            return -1;
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    if(reserve_array(&allocation, &model->position_capacity,
                     model->position_count + 1u,
                     sizeof(*model->positions)) < 0)
        return -1;
    model->positions = allocation;
    model->positions[model->position_count++] = position;
    return 0;
}

static int append_texcoord(source_model_t *model, char *cursor, int flip_v) {
    source_texcoord_t texcoord;
    void *allocation = model->texcoords;
    size_t component;

    for(component = 0; component < 2u; ++component) {
        char *token = next_token(&cursor);

        if(!token || parse_float_token(token, &texcoord.value[component]) < 0)
            return -1;
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    if(flip_v)
        texcoord.value[1] = 1.0f - texcoord.value[1];
    if(reserve_array(&allocation, &model->texcoord_capacity,
                     model->texcoord_count + 1u,
                     sizeof(*model->texcoords)) < 0)
        return -1;
    model->texcoords = allocation;
    model->texcoords[model->texcoord_count++] = texcoord;
    return 0;
}

static int append_normal(source_model_t *model, char *cursor) {
    source_normal_t normal;
    void *allocation = model->normals;
    double length_squared = 0.0;
    double inverse_length;
    size_t component;

    for(component = 0; component < 3u; ++component) {
        char *token = next_token(&cursor);

        if(!token || parse_float_token(token, &normal.value[component]) < 0)
            return -1;
        length_squared += (double)normal.value[component] *
                          (double)normal.value[component];
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    if(!(length_squared > 0.0) || !isfinite(length_squared)) {
        errno = ERANGE;
        return -1;
    }
    inverse_length = 1.0 / sqrt(length_squared);
    for(component = 0; component < 3u; ++component)
        normal.value[component] =
            (float)((double)normal.value[component] * inverse_length);

    if(reserve_array(&allocation, &model->normal_capacity,
                     model->normal_count + 1u,
                     sizeof(*model->normals)) < 0)
        return -1;
    model->normals = allocation;
    model->normals[model->normal_count++] = normal;
    return 0;
}

static int uv_fits(const source_model_t *model,
                   const source_triangle_t *triangle,
                   unsigned int fractional_bits) {
    double scale = (double)(UINT32_C(1) << fractional_bits);
    size_t corner;
    size_t component;

    for(corner = 0; corner < 3u; ++corner) {
        const source_texcoord_t *texcoord =
            &model->texcoords[triangle->corner[corner].texcoord];

        for(component = 0; component < 2u; ++component) {
            double scaled = (double)texcoord->value[component] * scale;

            if(scaled <= (double)INT16_MIN - 0.5 ||
               scaled >= (double)INT16_MAX + 0.5)
                return 0;
        }
    }
    return 1;
}

static int triangle_type(const source_model_t *model,
                         source_triangle_t *triangle) {
    int has_texcoord = triangle->corner[0].texcoord != SIZE_MAX;
    int has_normal = triangle->corner[0].normal != SIZE_MAX;
    size_t corner;

    for(corner = 1; corner < 3u; ++corner) {
        if((triangle->corner[corner].texcoord != SIZE_MAX) != has_texcoord ||
           (triangle->corner[corner].normal != SIZE_MAX) != has_normal) {
            errno = EILSEQ;
            return -1;
        }
    }
    if(has_texcoord) {
        int uv10 = uv_fits(model, triangle, 10u);
        int uv8 = uv10 || uv_fits(model, triangle, 8u);

        if(has_normal)
            triangle->strip_type = uv10 ?
                PVR_CHUNK_STRIP_UV10_FIXED_NORMAL : uv8 ?
                PVR_CHUNK_STRIP_UV8_FIXED_NORMAL :
                PVR_CHUNK_STRIP_UV_FLOAT_NORMAL;
        else
            triangle->strip_type = uv10 ? PVR_CHUNK_STRIP_UV10_FIXED : uv8 ?
                PVR_CHUNK_STRIP_UV8_FIXED : PVR_CHUNK_STRIP_UV_FLOAT;
    }
    else if(has_normal)
        triangle->strip_type = PVR_CHUNK_STRIP_NORMAL;
    else
        triangle->strip_type = PVR_CHUNK_STRIP_INDEX;
    return 0;
}

static int append_triangle(source_model_t *model, char *cursor,
                           int flip_winding, int texture_identifier,
                           size_t material_definition) {
    source_triangle_t triangle;
    void *allocation = model->triangles;
    size_t corner;

    for(corner = 0; corner < 3u; ++corner) {
        char *token = next_token(&cursor);

        if(!token || parse_corner(token, model, &triangle.corner[corner]) < 0)
            return -1;
    }
    if(next_token(&cursor)) {
        errno = ENOTSUP;
        return -1;
    }
    if(triangle_type(model, &triangle) < 0)
        return -1;
    if(texture_identifier < -1) {
        errno = ENOENT;
        return -1;
    }
    triangle.texture_identifier = texture_identifier;
    triangle.material_definition = material_definition;
    if(flip_winding) {
        source_corner_t temporary = triangle.corner[1];

        triangle.corner[1] = triangle.corner[2];
        triangle.corner[2] = temporary;
    }
    if(reserve_array(&allocation, &model->triangle_capacity,
                     model->triangle_count + 1u,
                     sizeof(*model->triangles)) < 0)
        return -1;
    model->triangles = allocation;
    model->triangles[model->triangle_count++] = triangle;
    return 0;
}

static int select_material(char *cursor, const material_table_t *materials,
                           const material_library_t *library,
                           int *texture_identifier,
                           size_t *material_definition) {
    char *name = next_token(&cursor);

    if(!materials->count && !library->count) {
        errno = ENOTSUP;
        return -1;
    }
    if(!name || next_token(&cursor)) {
        errno = EILSEQ;
        return -1;
    }
    if(materials->count &&
       material_table_find(materials, name, texture_identifier) < 0) {
        errno = EILSEQ;
        return -1;
    }
    if(library->count) {
        if(material_definition_find(library, name,
                                    material_definition) < 0 ||
           !material_definition_complete(
               &library->definitions[*material_definition])) {
            errno = EILSEQ;
            return -1;
        }
    }
    else
        *material_definition = SIZE_MAX;
    return 0;
}

static int validate_references(const source_model_t *model) {
    size_t triangle;
    size_t corner;

    if(!model->position_count || !model->triangle_count ||
       (model->has_colors && model->color_count != model->position_count)) {
        errno = EILSEQ;
        return -1;
    }
    for(triangle = 0; triangle < model->triangle_count; ++triangle) {
        for(corner = 0; corner < 3u; ++corner) {
            const source_corner_t *reference =
                &model->triangles[triangle].corner[corner];

            if(reference->position >= model->position_count ||
               reference->position > UINT16_MAX ||
               (reference->texcoord != SIZE_MAX &&
                reference->texcoord >= model->texcoord_count) ||
               (reference->normal != SIZE_MAX &&
                reference->normal >= model->normal_count)) {
                errno = EILSEQ;
                return -1;
            }
        }
    }
    return 0;
}

static int validate_texture_policy(const source_model_t *model) {
    int saw_textured = 0;
    int saw_untextured = 0;
    size_t triangle;

    for(triangle = 0; triangle < model->triangle_count; ++triangle) {
        int has_texcoord =
            model->triangles[triangle].corner[0].texcoord != SIZE_MAX;
        int has_texture =
            model->triangles[triangle].texture_identifier >= 0;

        if(has_texcoord != has_texture) {
            errno = EILSEQ;
            return -1;
        }
        if(!has_texcoord)
            saw_untextured = 1;
        else
            saw_textured = 1;
    }
    /* Compact texture state persists, so there is no implicit transition back
       to untextured rendering after the first texture record. */
    if(saw_textured && saw_untextured) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int validate_material_policy(const source_model_t *model,
                                    const material_library_t *library) {
    size_t triangle;

    if(!library->count)
        return 0;
    for(triangle = 0; triangle < model->triangle_count; ++triangle) {
        size_t definition = model->triangles[triangle].material_definition;

        if(definition >= library->count ||
           !material_definition_complete(&library->definitions[definition])) {
            errno = EILSEQ;
            return -1;
        }
    }
    return 0;
}

static int load_obj_source(const char *path, source_model_t *model,
                           int flip_winding, int flip_v,
                           int texture_identifier,
                           const material_table_t *materials,
                           const material_library_t *library,
                           size_t *error_line) {
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_length;
    size_t line_number = 0;
    int active_texture = materials->count ? -2 : texture_identifier;
    size_t active_material = SIZE_MAX;
    int rv;
    int saved_errno;

    if(!file)
        return -1;
    errno = 0;
    while((rv = read_line(file, &line, &line_capacity, &line_length)) > 0) {
        char *cursor = line;
        char *directive;

        (void)line_length;
        ++line_number;
        directive = next_token(&cursor);
        if(!directive)
            continue;

        if(!strcmp(directive, "v"))
            rv = append_position(model, cursor);
        else if(!strcmp(directive, "vt"))
            rv = append_texcoord(model, cursor, flip_v);
        else if(!strcmp(directive, "vn"))
            rv = append_normal(model, cursor);
        else if(!strcmp(directive, "f"))
            rv = append_triangle(model, cursor, flip_winding,
                                 active_texture, active_material);
        else if(!strcmp(directive, "usemtl"))
            rv = select_material(cursor, materials, library,
                                 &active_texture, &active_material);
        else if(!strcmp(directive, "mtllib"))
            rv = 0;
        else if(!strcmp(directive, "o") || !strcmp(directive, "g") ||
                !strcmp(directive, "s"))
            rv = 0;
        else {
            errno = ENOTSUP;
            rv = -1;
        }

        if(rv < 0) {
            *error_line = line_number;
            goto fail;
        }
    }
    if(rv < 0) {
        if(!errno)
            errno = EIO;
        *error_line = line_number + 1u;
        goto fail;
    }
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    file = NULL;
    free(line);
    if(validate_references(model) < 0) {
        *error_line = line_number;
        return -1;
    }
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    free(line);
    errno = saved_errno;
    return -1;
}

static size_t strip_vertex_words(uint8_t type) {
    switch(type) {
        case PVR_CHUNK_STRIP_INDEX:
            return 1u;
        case PVR_CHUNK_STRIP_UV8_FIXED:
        case PVR_CHUNK_STRIP_UV10_FIXED:
            return 3u;
        case PVR_CHUNK_STRIP_UV_FLOAT:
            return 5u;
        case PVR_CHUNK_STRIP_NORMAL:
            return 4u;
        case PVR_CHUNK_STRIP_UV8_FIXED_NORMAL:
        case PVR_CHUNK_STRIP_UV10_FIXED_NORMAL:
            return 6u;
        case PVR_CHUNK_STRIP_UV_FLOAT_NORMAL:
            return 8u;
        default:
            return 0;
    }
}

static int corners_equal(const source_corner_t *left,
                         const source_corner_t *right) {
    return left->position == right->position &&
           left->texcoord == right->texcoord &&
           left->normal == right->normal;
}

static size_t maximum_strip_vertices(uint8_t type) {
    size_t stride = strip_vertex_words(type);
    size_t maximum;

    if(!stride)
        return 0;
    maximum = (UINT16_MAX - 2u) / stride;

    if(maximum > UINT16_C(0x7fff))
        maximum = UINT16_C(0x7fff);
    return maximum;
}

static int append_strip(strip_plan_t *plan, const source_strip_t *strip) {
    if(reserve_array((void **)&plan->strips, &plan->capacity,
                     plan->count + 1u, sizeof(*plan->strips)) < 0)
        return -1;
    plan->strips[plan->count++] = *strip;
    return 0;
}

static int build_strip_plan(const source_model_t *model, int join_strips,
                            strip_plan_t *plan) {
    size_t first = 0;

    while(first < model->triangle_count) {
        const source_triangle_t *initial = &model->triangles[first];
        source_corner_t second_last = initial->corner[1];
        source_corner_t last = initial->corner[2];
        source_strip_t strip = {
            .first_triangle = first,
            .triangle_count = 1u,
            .vertex_count = 3u,
            .strip_type = initial->strip_type,
            .texture_identifier = initial->texture_identifier,
            .material_definition = initial->material_definition
        };
        size_t maximum = maximum_strip_vertices(initial->strip_type);

        if(maximum < 3u) {
            errno = EPROTO;
            return -1;
        }

        while(join_strips && first + strip.triangle_count <
              model->triangle_count && strip.vertex_count < maximum) {
            const source_triangle_t *next =
                &model->triangles[first + strip.triangle_count];
            const source_corner_t *expected_first;
            const source_corner_t *expected_second;
            size_t triangle_ordinal = strip.vertex_count - 2u;

            if(next->strip_type != strip.strip_type ||
               next->texture_identifier != strip.texture_identifier ||
               next->material_definition != strip.material_definition)
                break;

            /* PVR triangle strips alternate winding. Preserve OBJ face order
               by accepting only the exact next edge orientation required by
               that alternation; per-corner attributes are part of identity. */
            if(triangle_ordinal & 1u) {
                expected_first = &last;
                expected_second = &second_last;
            }
            else {
                expected_first = &second_last;
                expected_second = &last;
            }
            if(!corners_equal(&next->corner[0], expected_first) ||
               !corners_equal(&next->corner[1], expected_second))
                break;

            second_last = last;
            last = next->corner[2];
            ++strip.triangle_count;
            ++strip.vertex_count;
        }

        if(append_strip(plan, &strip) < 0)
            return -1;
        first += strip.triangle_count;
    }
    return 0;
}

static size_t strip_batch(const strip_plan_t *plan, size_t first) {
    size_t stride = strip_vertex_words(plan->strips[first].strip_type);
    size_t payload_words = 1u;
    size_t count = 1u;

    payload_words += 1u + plan->strips[first].vertex_count * stride;
    while(count < MAX_STRIP_COUNT && first + count < plan->count &&
          plan->strips[first + count].strip_type ==
              plan->strips[first].strip_type &&
          plan->strips[first + count].texture_identifier ==
              plan->strips[first].texture_identifier &&
          plan->strips[first + count].material_definition ==
              plan->strips[first].material_definition) {
        size_t addition =
            1u + plan->strips[first + count].vertex_count * stride;

        if(addition > UINT16_MAX - payload_words)
            break;
        payload_words += addition;
        ++count;
    }
    return count;
}

static size_t material_value_count(
    const material_definition_t *definition) {
    return 1u + !!(definition->present & MATERIAL_AMBIENT) +
           !!(definition->present & MATERIAL_SPECULAR);
}

static uint8_t material_record_type(
    const material_definition_t *definition) {
    int ambient = !!(definition->present & MATERIAL_AMBIENT);
    int specular = !!(definition->present & MATERIAL_SPECULAR);

    if(ambient && specular)
        return PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT_SPECULAR;
    if(ambient)
        return PVR_CHUNK_MATERIAL_DIFFUSE_AMBIENT;
    if(specular)
        return PVR_CHUNK_MATERIAL_DIFFUSE_SPECULAR;
    return PVR_CHUNK_MATERIAL_DIFFUSE;
}

static int calculate_sizes(const source_model_t *model,
                           const strip_plan_t *plan,
                           const material_library_t *library,
                           output_streams_t *streams) {
    size_t vertex_batches =
        (model->position_count + MAX_VERTEX_BATCH - 1u) / MAX_VERTEX_BATCH;
    size_t vertex_stride = model->has_colors ? 4u : 3u;
    size_t vertex_words;
    size_t polygon_words = 1u;
    size_t first = 0;
    int active_texture = -1;
    size_t active_material = SIZE_MAX;

    if(model->position_count >
       (SIZE_MAX - 1u - 2u * vertex_batches) / vertex_stride) {
        errno = EOVERFLOW;
        return -1;
    }
    vertex_words = 1u + 2u * vertex_batches +
                   vertex_stride * model->position_count;

    if(!library->count) {
        polygon_words += 4u;
        ++streams->material_record_count;
    }

    while(first < plan->count) {
        size_t count = strip_batch(plan, first);
        size_t stride = strip_vertex_words(plan->strips[first].strip_type);
        size_t payload_words = 1u;
        size_t strip;
        size_t addition;
        int texture_identifier = plan->strips[first].texture_identifier;
        size_t material_definition =
            plan->strips[first].material_definition;

        for(strip = 0; strip < count; ++strip)
            payload_words += 1u +
                plan->strips[first + strip].vertex_count * stride;
        addition = 2u + payload_words;

        if(library->count && material_definition != active_material) {
            size_t material_words = 2u + 2u * material_value_count(
                &library->definitions[material_definition]);

            if(material_words > SIZE_MAX - polygon_words) {
                errno = EOVERFLOW;
                return -1;
            }
            polygon_words += material_words;
            ++streams->material_record_count;
            active_material = material_definition;
        }

        if(texture_identifier >= 0 && texture_identifier != active_texture) {
            if(polygon_words > SIZE_MAX - 2u) {
                errno = EOVERFLOW;
                return -1;
            }
            polygon_words += 2u;
            ++streams->texture_record_count;
            active_texture = texture_identifier;
        }

        if(addition > SIZE_MAX - polygon_words) {
            errno = EOVERFLOW;
            return -1;
        }
        polygon_words += addition;
        ++streams->strip_record_count;
        first += count;
    }
    if(vertex_words > SIZE_MAX / sizeof(uint32_t) ||
       polygon_words > SIZE_MAX / sizeof(uint16_t)) {
        errno = EOVERFLOW;
        return -1;
    }
    streams->vertex_word_count = vertex_words;
    streams->polygon_word_count = polygon_words;
    return 0;
}

static uint32_t float_word(float value) {
    uint32_t word;

    memcpy(&word, &value, sizeof(word));
    return word;
}

static uint16_t quantize_uv(float value, unsigned int fractional_bits) {
    long encoded = lround((double)value *
                          (double)(UINT32_C(1) << fractional_bits));

    return (uint16_t)(int16_t)encoded;
}

static uint16_t quantize_normal(float value) {
    long encoded;

    if(value <= -1.0f)
        return UINT16_C(0x8000);
    if(value >= 1.0f)
        return UINT16_C(0x7fff);
    encoded = lroundf(value * 32767.0f);
    return (uint16_t)(int16_t)encoded;
}

static uint32_t quantize_color(const float color[3]) {
    uint32_t red = (uint32_t)lroundf(color[0] * 255.0f);
    uint32_t green = (uint32_t)lroundf(color[1] * 255.0f);
    uint32_t blue = (uint32_t)lroundf(color[2] * 255.0f);

    return UINT32_C(0xff000000) | (red << 16) | (green << 8) | blue;
}

static uint32_t quantize_material_diffuse(
    const material_definition_t *definition) {
    uint32_t alpha = (uint32_t)lroundf(definition->alpha * 255.0f);

    return (alpha << 24) |
           (quantize_color(definition->diffuse) & UINT32_C(0x00ffffff));
}

static uint32_t quantize_argb(const float color[4]) {
    uint32_t alpha = (uint32_t)lroundf(color[3] * 255.0f);
    uint32_t red = (uint32_t)lroundf(color[0] * 255.0f);
    uint32_t green = (uint32_t)lroundf(color[1] * 255.0f);
    uint32_t blue = (uint32_t)lroundf(color[2] * 255.0f);

    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

static void emit_u32(uint16_t **output, uint32_t value) {
    *(*output)++ = (uint16_t)value;
    *(*output)++ = (uint16_t)(value >> 16);
}

static void emit_material(uint16_t **output,
                          const material_definition_t *definition) {
    size_t value_count = material_value_count(definition);
    uint8_t blend = definition->alpha_mode == MATERIAL_ALPHA_BLEND ?
        (uint8_t)((PVR_BLEND_SRCALPHA << 3) | PVR_BLEND_INVSRCALPHA) :
        (uint8_t)((PVR_BLEND_ONE << 3) | PVR_BLEND_ZERO);

    *(*output)++ = (uint16_t)material_record_type(definition) |
                  (uint16_t)((uint16_t)blend << 8);
    *(*output)++ = (uint16_t)(value_count * 2u);
    emit_u32(output, quantize_material_diffuse(definition));
    if(definition->present & MATERIAL_AMBIENT)
        emit_u32(output, quantize_color(definition->ambient));
    if(definition->present & MATERIAL_SPECULAR) {
        uint32_t specular = quantize_color(definition->specular) &
                            UINT32_C(0x00ffffff);
        uint32_t exponent = (uint32_t)lroundf(
            definition->exponent * (16.0f / 1000.0f));

        emit_u32(output, specular | (exponent << 24));
    }
}

static int calculate_bounds(const source_model_t *model,
                            output_streams_t *streams) {
    float minimum[3];
    float maximum[3];
    double radius = 0.0;
    size_t position;
    size_t component;

    memcpy(minimum, model->positions[0].value, sizeof(minimum));
    memcpy(maximum, model->positions[0].value, sizeof(maximum));
    for(position = 1; position < model->position_count; ++position) {
        for(component = 0; component < 3u; ++component) {
            float value = model->positions[position].value[component];

            if(value < minimum[component])
                minimum[component] = value;
            if(value > maximum[component])
                maximum[component] = value;
        }
    }
    for(component = 0; component < 3u; ++component) {
        double center = ((double)minimum[component] +
                         (double)maximum[component]) * 0.5;

        if(!isfinite(center) || fabs(center) > FLT_MAX) {
            errno = ERANGE;
            return -1;
        }
        streams->center[component] = (float)center;
        if(streams->center[component] == 0.0f)
            streams->center[component] = 0.0f;
    }
    for(position = 0; position < model->position_count; ++position) {
        double length_squared = 0.0;

        for(component = 0; component < 3u; ++component) {
            double difference =
                (double)model->positions[position].value[component] -
                (double)streams->center[component];

            length_squared += difference * difference;
        }
        if(sqrt(length_squared) > radius)
            radius = sqrt(length_squared);
    }
    if(!isfinite(radius) || radius > FLT_MAX) {
        errno = ERANGE;
        return -1;
    }
    streams->radius = (float)radius;
    return 0;
}

static void emit_corner(uint16_t **output, const source_model_t *model,
                        const source_corner_t *corner, uint8_t type) {
    *(*output)++ = (uint16_t)corner->position;
    if(type == PVR_CHUNK_STRIP_UV8_FIXED ||
       type == PVR_CHUNK_STRIP_UV10_FIXED ||
       type == PVR_CHUNK_STRIP_UV8_FIXED_NORMAL ||
       type == PVR_CHUNK_STRIP_UV10_FIXED_NORMAL) {
        const source_texcoord_t *texcoord = &model->texcoords[corner->texcoord];
        unsigned int fractional_bits =
            type == PVR_CHUNK_STRIP_UV8_FIXED ||
            type == PVR_CHUNK_STRIP_UV8_FIXED_NORMAL ? 8u : 10u;

        *(*output)++ = quantize_uv(texcoord->value[0], fractional_bits);
        *(*output)++ = quantize_uv(texcoord->value[1], fractional_bits);
    }
    else if(type == PVR_CHUNK_STRIP_UV_FLOAT ||
            type == PVR_CHUNK_STRIP_UV_FLOAT_NORMAL) {
        const source_texcoord_t *texcoord = &model->texcoords[corner->texcoord];

        emit_u32(output, float_word(texcoord->value[0]));
        emit_u32(output, float_word(texcoord->value[1]));
    }
    if(type == PVR_CHUNK_STRIP_NORMAL ||
       type == PVR_CHUNK_STRIP_UV8_FIXED_NORMAL ||
       type == PVR_CHUNK_STRIP_UV10_FIXED_NORMAL ||
       type == PVR_CHUNK_STRIP_UV_FLOAT_NORMAL) {
        const source_normal_t *normal = &model->normals[corner->normal];
        size_t component;

        for(component = 0; component < 3u; ++component)
            *(*output)++ = quantize_normal(normal->value[component]);
    }
}

static int generate_streams(const source_model_t *model,
                            const material_library_t *library,
                            int join_strips, output_streams_t *streams) {
    strip_plan_t plan = { 0 };
    uint32_t *vertex_output;
    uint16_t *polygon_output;
    size_t first;
    int active_texture = -1;
    size_t active_material = SIZE_MAX;

    if(build_strip_plan(model, join_strips, &plan) < 0 ||
       calculate_sizes(model, &plan, library, streams) < 0 ||
       calculate_bounds(model, streams) < 0)
        goto fail;
    streams->source_strip_count = model->triangle_count;
    streams->output_strip_count = plan.count;
    streams->vertex_words =
        malloc(streams->vertex_word_count * sizeof(*streams->vertex_words));
    streams->polygon_words =
        malloc(streams->polygon_word_count * sizeof(*streams->polygon_words));
    if(!streams->vertex_words || !streams->polygon_words) {
        errno = ENOMEM;
        goto fail;
    }

    /* Position batches cover one unique, contiguous 16-bit index namespace. */
    vertex_output = streams->vertex_words;
    first = 0;
    while(first < model->position_count) {
        size_t count = model->position_count - first;
        size_t vertex_stride = model->has_colors ? 4u : 3u;
        size_t payload_words;
        size_t position;

        if(count > MAX_VERTEX_BATCH)
            count = MAX_VERTEX_BATCH;
        payload_words = 1u + vertex_stride * count;
        *vertex_output++ = (model->has_colors ?
                            PVR_CHUNK_VERTEX_XYZ_ARGB :
                            PVR_CHUNK_VERTEX_XYZ) |
                           ((uint32_t)payload_words << 16);
        *vertex_output++ = ((uint32_t)count << 16) | (uint32_t)first;
        for(position = 0; position < count; ++position) {
            size_t component;

            for(component = 0; component < 3u; ++component) {
                *vertex_output++ = float_word(
                    model->positions[first + position].value[component]);
            }
            if(model->has_colors)
                *vertex_output++ = quantize_argb(
                    model->colors[first + position].value);
        }
        first += count;
    }
    *vertex_output++ = PVR_CHUNK_CONTROL_END;

    /* Without explicit material properties, preserve the original white
       diffuse state. Explicit definitions replace it at source transitions. */
    polygon_output = streams->polygon_words;
    if(!library->count) {
        *polygon_output++ = PVR_CHUNK_MATERIAL_DIFFUSE |
            (uint16_t)(((PVR_BLEND_ONE << 3) | PVR_BLEND_ZERO) << 8);
        *polygon_output++ = 2u;
        *polygon_output++ = UINT16_MAX;
        *polygon_output++ = UINT16_MAX;
    }
    first = 0;
    while(first < plan.count) {
        size_t count = strip_batch(&plan, first);
        uint8_t type = plan.strips[first].strip_type;
        size_t stride = strip_vertex_words(type);
        size_t payload_words = 1u;
        size_t strip;
        int texture_identifier = plan.strips[first].texture_identifier;
        size_t material_definition =
            plan.strips[first].material_definition;

        for(strip = 0; strip < count; ++strip)
            payload_words += 1u +
                plan.strips[first + strip].vertex_count * stride;

        if(library->count && material_definition != active_material) {
            emit_material(&polygon_output,
                          &library->definitions[material_definition]);
            active_material = material_definition;
        }

        if(texture_identifier >= 0 && texture_identifier != active_texture) {
            *polygon_output++ = PVR_CHUNK_TEXTURE;
            *polygon_output++ = (uint16_t)texture_identifier;
            active_texture = texture_identifier;
        }

        {
            uint8_t strip_flags = 0;

            if(library->count) {
                const material_definition_t *definition =
                    &library->definitions[material_definition];

                if(definition->alpha_mode != MATERIAL_ALPHA_OPAQUE)
                    strip_flags |= PVR_CHUNK_STRIP_USE_ALPHA;
                if(definition->double_sided)
                    strip_flags |= PVR_CHUNK_STRIP_DOUBLE_SIDED;
            }
            *polygon_output++ = (uint16_t)type |
                                (uint16_t)((uint16_t)strip_flags << 8);
        }
        *polygon_output++ = (uint16_t)payload_words;
        *polygon_output++ = (uint16_t)count;
        for(strip = 0; strip < count; ++strip) {
            const source_strip_t *source_strip = &plan.strips[first + strip];
            const source_triangle_t *initial =
                &model->triangles[source_strip->first_triangle];
            size_t triangle;
            size_t corner;

            *polygon_output++ = (uint16_t)source_strip->vertex_count;
            for(corner = 0; corner < 3u; ++corner)
                emit_corner(&polygon_output, model, &initial->corner[corner],
                            type);
            for(triangle = 1; triangle < source_strip->triangle_count;
                ++triangle) {
                const source_triangle_t *source = &model->triangles[
                    source_strip->first_triangle + triangle];

                emit_corner(&polygon_output, model, &source->corner[2], type);
            }
        }
        first += count;
    }
    *polygon_output++ = PVR_CHUNK_CONTROL_END;

    if((size_t)(vertex_output - streams->vertex_words) !=
           streams->vertex_word_count ||
       (size_t)(polygon_output - streams->polygon_words) !=
           streams->polygon_word_count) {
        errno = EPROTO;
        goto fail;
    }
    strip_plan_free(&plan);
    return 0;

fail:
    strip_plan_free(&plan);
    return -1;
}

static int validate_generated(const source_model_t *source,
                              const output_streams_t *streams,
                              pvr_chunk_model_info_t *info) {
    pvr_chunk_model_t model = {
        .vertex_words = streams->vertex_words,
        .vertex_word_count = streams->vertex_word_count,
        .polygon_words = streams->polygon_words,
        .polygon_word_count = streams->polygon_word_count,
        .center = { streams->center[0], streams->center[1],
                    streams->center[2] },
        .radius = streams->radius
    };

    if(pvr_chunk_model_validate(&model, info) < 0)
        return -1;
    if(info->vertex_entries != source->position_count ||
       info->triangles != source->triangle_count ||
       info->index_references != source->triangle_count +
                                  2u * streams->output_strip_count ||
       info->strip_records != streams->strip_record_count ||
       info->material_records != streams->material_record_count) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int write_word(FILE *file, uint32_t value, size_t width) {
    unsigned char bytes[4] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)
    };

    if(fwrite(bytes, 1, width, file) != width) {
        if(!errno)
            errno = EIO;
        return -1;
    }
    return 0;
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

static int align_size_32(size_t value, size_t *result) {
    if(value > SIZE_MAX - (PVR_CHUNK_ASSET_ALIGNMENT - 1u)) {
        errno = EOVERFLOW;
        return -1;
    }
    *result = (value + PVR_CHUNK_ASSET_ALIGNMENT - 1u) &
              ~(size_t)(PVR_CHUNK_ASSET_ALIGNMENT - 1u);
    return 0;
}

typedef struct compiled_texture_image {
    uint16_t identifier;
    uint16_t width;
    uint16_t height;
    pvr_txr_surface_format_t format;
    uint8_t *data;
    size_t data_size;
} compiled_texture_image_t;

static int texture_dimension_valid(int value) {
    return value >= 8 && value <= 1024 &&
           !(value & (value - 1));
}

static size_t twiddle_bits(size_t value) {
    size_t spread = 0;
    unsigned bit;

    for(bit = 0; bit < 10; ++bit)
        spread |= ((value >> bit) & 1u) << (bit * 2u);
    return spread;
}

static size_t twiddled_index(size_t x, size_t y,
                             size_t width, size_t height) {
    size_t minimum = width < height ? width : height;
    size_t mask = minimum - 1u;

    return (twiddle_bits(y & mask) | (twiddle_bits(x & mask) << 1u)) +
           (x / minimum + y / minimum) * minimum * minimum;
}

static int data_uri_decode(const char *uri, uint8_t **data,
                           size_t *data_size) {
    cgltf_options options = { 0 };
    const char *comma = strchr(uri, ',');
    const char *encoded;
    size_t encoded_size;
    size_t padding = 0;
    size_t decoded_size;
    cgltf_result result;

    *data = NULL;
    *data_size = 0;
    if(!comma || comma - uri < 7 ||
       strncmp(comma - 7, ";base64", 7)) {
        errno = ENOTSUP;
        return -1;
    }
    encoded = comma + 1;
    encoded_size = strlen(encoded);
    if(!encoded_size || (encoded_size & 3u)) {
        errno = EILSEQ;
        return -1;
    }
    if(encoded[encoded_size - 1u] == '=')
        ++padding;
    if(encoded_size > 1u && encoded[encoded_size - 2u] == '=')
        ++padding;
    if(encoded_size / 4u > (SIZE_MAX - 2u) / 3u) {
        errno = EOVERFLOW;
        return -1;
    }
    decoded_size = encoded_size / 4u * 3u - padding;
    result = cgltf_load_buffer_base64(
        &options, decoded_size, encoded, (void **)data);
    if(result != cgltf_result_success) {
        errno = cgltf_errno(result);
        return -1;
    }
    *data_size = decoded_size;
    return 0;
}

static char *image_path_resolve(const char *source_path,
                                const char *uri) {
    const char *forward = strrchr(source_path, '/');
    const char *backward = strrchr(source_path, '\\');
    const char *separator = forward && backward ?
        (forward > backward ? forward : backward) :
        (forward ? forward : backward);
    size_t prefix = separator ? (size_t)(separator - source_path) + 1u : 0u;
    size_t uri_size = strlen(uri);
    char *path;

    if(prefix > SIZE_MAX - uri_size - 1u) {
        errno = EOVERFLOW;
        return NULL;
    }
    path = malloc(prefix + uri_size + 1u);
    if(!path) {
        errno = ENOMEM;
        return NULL;
    }
    if(prefix)
        memcpy(path, source_path, prefix);
    memcpy(path + prefix, uri, uri_size + 1u);
    cgltf_decode_uri(path + prefix);
    return path;
}

static int gltf_image_rgba_load(const cgltf_image *image,
                                const char *source_path,
                                stbi_uc **pixels, int *width, int *height) {
    const uint8_t *encoded = NULL;
    uint8_t *owned_encoded = NULL;
    size_t encoded_size = 0;
    char *path = NULL;
    int components;

    *pixels = NULL;
    *width = 0;
    *height = 0;
    if(!image) {
        errno = EILSEQ;
        return -1;
    }
    if(image->buffer_view) {
        encoded = cgltf_buffer_view_data(image->buffer_view);
        encoded_size = image->buffer_view->size;
        if(!encoded) {
            errno = EILSEQ;
            return -1;
        }
    }
    else if(image->uri && !strncmp(image->uri, "data:", 5)) {
        if(data_uri_decode(image->uri, &owned_encoded,
                           &encoded_size) < 0)
            return -1;
        encoded = owned_encoded;
    }
    else if(image->uri) {
        path = image_path_resolve(source_path, image->uri);
        if(!path)
            return -1;
        *pixels = stbi_load(path, width, height, &components, 4);
    }
    else {
        errno = EILSEQ;
        return -1;
    }

    if(encoded) {
        if(encoded_size > INT_MAX) {
            free(owned_encoded);
            errno = EOVERFLOW;
            return -1;
        }
        *pixels = stbi_load_from_memory(
            encoded, (int)encoded_size, width, height, &components, 4);
    }
    free(path);
    free(owned_encoded);
    if(!*pixels) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int compile_texture_image(const cgltf_image *source,
                                 const char *source_path,
                                 uint16_t identifier,
                                 compiled_texture_image_t *image) {
    stbi_uc *rgba = NULL;
    uint8_t *encoded = NULL;
    int width;
    int height;
    size_t pixel_count;
    size_t pixel;
    int has_alpha = 0;
    int has_partial_alpha = 0;

    memset(image, 0, sizeof(*image));
    if(gltf_image_rgba_load(source, source_path, &rgba,
                            &width, &height) < 0)
        return -1;
    if(!texture_dimension_valid(width) ||
       !texture_dimension_valid(height)) {
        stbi_image_free(rgba);
        errno = ENOTSUP;
        return -1;
    }
    pixel_count = (size_t)width * (size_t)height;
    if(pixel_count > SIZE_MAX / 2u) {
        stbi_image_free(rgba);
        errno = EOVERFLOW;
        return -1;
    }
    for(pixel = 0; pixel < pixel_count; ++pixel) {
        uint8_t alpha = rgba[pixel * 4u + 3u];

        if(alpha != UINT8_MAX)
            has_alpha = 1;
        if(alpha && alpha != UINT8_MAX)
            has_partial_alpha = 1;
    }
    encoded = malloc(pixel_count * 2u);
    if(!encoded) {
        stbi_image_free(rgba);
        errno = ENOMEM;
        return -1;
    }
    for(pixel = 0; pixel < pixel_count; ++pixel) {
        size_t x = pixel % (size_t)width;
        size_t y = pixel / (size_t)width;
        size_t output = twiddled_index(
            x, y, (size_t)width, (size_t)height) * 2u;
        uint8_t red = rgba[pixel * 4u];
        uint8_t green = rgba[pixel * 4u + 1u];
        uint8_t blue = rgba[pixel * 4u + 2u];
        uint8_t alpha = rgba[pixel * 4u + 3u];
        uint16_t texel;

        if(!has_alpha)
            texel = (uint16_t)((uint16_t)(red >> 3) << 11) |
                    (uint16_t)((uint16_t)(green >> 2) << 5) |
                    (uint16_t)(blue >> 3);
        else if(!has_partial_alpha)
            texel = (uint16_t)((uint16_t)(alpha >= 128u) << 15) |
                    (uint16_t)((uint16_t)(red >> 3) << 10) |
                    (uint16_t)((uint16_t)(green >> 3) << 5) |
                    (uint16_t)(blue >> 3);
        else
            texel = (uint16_t)((uint16_t)(alpha >> 4) << 12) |
                    (uint16_t)((uint16_t)(red >> 4) << 8) |
                    (uint16_t)((uint16_t)(green >> 4) << 4) |
                    (uint16_t)(blue >> 4);
        store_le16(encoded + output, texel);
    }
    stbi_image_free(rgba);
    image->identifier = identifier;
    image->width = (uint16_t)width;
    image->height = (uint16_t)height;
    image->format = !has_alpha ? PVR_TXR_SURFACE_RGB565 :
                    (!has_partial_alpha ? PVR_TXR_SURFACE_ARGB1555 :
                                          PVR_TXR_SURFACE_ARGB4444);
    image->data = encoded;
    image->data_size = pixel_count * 2u;
    return 0;
}

static int serialize_gltf_textures(
        const cgltf_data *data, const char *source_path,
        int texture_override, uint8_t **bytes_out, size_t *size_out) {
    uint8_t *used = NULL;
    compiled_texture_image_t *images = NULL;
    uint8_t *bytes = NULL;
    size_t image_count = 0;
    size_t data_offset;
    size_t file_bytes;
    size_t material;
    size_t texture;
    size_t output = 0;
    pvr_chunk_texture_section_view_t checked;
    int saved_errno;

    *bytes_out = NULL;
    *size_out = 0;
    if(texture_override >= 0 || !data->textures_count)
        return 0;
    used = calloc(data->textures_count, sizeof(*used));
    if(!used) {
        errno = ENOMEM;
        goto fail;
    }
    for(material = 0; material < data->materials_count; ++material) {
        const cgltf_material *source = &data->materials[material];
        const cgltf_texture *texture_pointer =
            source->has_pbr_metallic_roughness ?
            source->pbr_metallic_roughness.base_color_texture.texture : NULL;
        size_t ordinal;

        if(!texture_pointer)
            continue;
        if(gltf_array_index(data->textures, data->textures_count,
                           sizeof(*data->textures), texture_pointer,
                           &ordinal) < 0)
            goto fail;
        if(!used[ordinal]) {
            used[ordinal] = 1;
            ++image_count;
        }
    }
    if(!image_count) {
        free(used);
        return 0;
    }
    images = calloc(image_count, sizeof(*images));
    if(!images) {
        errno = ENOMEM;
        goto fail;
    }
    for(texture = 0; texture < data->textures_count; ++texture) {
        if(!used[texture])
            continue;
        if(texture > PVR_CHUNK_TEXTURE_IDENTIFIER_MAX ||
           compile_texture_image(data->textures[texture].image,
                                 source_path, (uint16_t)texture,
                                 &images[output++]) < 0)
            goto fail;
    }
    if(output != image_count ||
       image_count > (SIZE_MAX - PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES) /
                         PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES ||
       align_size_32(PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES +
                     image_count * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES,
                     &data_offset) < 0)
        goto fail;
    file_bytes = data_offset;
    for(texture = 0; texture < image_count; ++texture) {
        if(align_size_32(file_bytes, &file_bytes) < 0 ||
           file_bytes > SIZE_MAX - images[texture].data_size)
            goto fail;
        file_bytes += images[texture].data_size;
    }
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        goto fail;
    }
    file_bytes = data_offset;
    for(texture = 0; texture < image_count; ++texture) {
        uint8_t *entry = bytes + PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES +
            texture * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES;

        if(align_size_32(file_bytes, &file_bytes) < 0)
            goto fail;
        memcpy(bytes + file_bytes, images[texture].data,
               images[texture].data_size);
        store_le16(entry, images[texture].identifier);
        entry[2] = (uint8_t)images[texture].format;
        entry[3] = PVR_TXR_SURFACE_TWIDDLED;
        store_le16(entry + 4, images[texture].width);
        store_le16(entry + 6, images[texture].height);
        store_le32(entry + 12, (uint32_t)file_bytes);
        store_le32(entry + 16, (uint32_t)images[texture].data_size);
        store_le32(entry + 20, crc32_bytes(
            images[texture].data, images[texture].data_size));
        file_bytes += images[texture].data_size;
    }
    store_le32(bytes, PVR_CHUNK_TEXTURE_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_TEXTURE_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)image_count);
    store_le16(bytes + 16, PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES);
    store_le32(bytes + 20, (uint32_t)data_offset);
    store_le32(bytes + 24, crc32_bytes(
        bytes + PVR_CHUNK_TEXTURE_SECTION_HEADER_BYTES,
        image_count * PVR_CHUNK_TEXTURE_SECTION_ENTRY_BYTES));
    store_le32(bytes + 28, crc32_bytes(
        bytes + data_offset, file_bytes - data_offset));
    store_le32(bytes + 60, crc32_bytes(bytes, 60));
    if(pvr_chunk_texture_section_open(bytes, file_bytes, &checked) < 0)
        goto fail;

    for(texture = 0; texture < image_count; ++texture)
        free(images[texture].data);
    free(images);
    free(used);
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(images) {
        for(texture = 0; texture < image_count; ++texture)
            free(images[texture].data);
    }
    free(bytes);
    free(images);
    free(used);
    errno = saved_errno;
    return -1;
}

static int serialize_words(const void *words, size_t word_count,
                           size_t word_size, uint8_t **bytes_out,
                           size_t *size_out) {
    uint8_t *bytes;
    size_t size;
    size_t word;

    if((word_size != sizeof(uint16_t) && word_size != sizeof(uint32_t)) ||
       word_count > SIZE_MAX / word_size) {
        errno = EOVERFLOW;
        return -1;
    }
    size = word_count * word_size;
    bytes = malloc(size);
    if(!bytes) {
        errno = ENOMEM;
        return -1;
    }
    for(word = 0; word < word_count; ++word) {
        uint32_t value = word_size == sizeof(uint32_t) ?
            ((const uint32_t *)words)[word] :
            ((const uint16_t *)words)[word];

        if(word_size == sizeof(uint32_t))
            store_le32(bytes + word * word_size, value);
        else
            store_le16(bytes + word * word_size, (uint16_t)value);
    }
    *bytes_out = bytes;
    *size_out = size;
    return 0;
}

static void store_pcm2_section(uint8_t *descriptor, uint32_t type,
                               size_t offset, size_t stored_bytes,
                               size_t decoded_bytes, uint32_t decoded_crc32,
                               pvr_chunk_asset_codec_t codec,
                               uint16_t alignment) {
    store_le32(descriptor, type);
    store_le32(descriptor + 8, (uint32_t)offset);
    store_le32(descriptor + 12, (uint32_t)stored_bytes);
    store_le32(descriptor + 16, (uint32_t)decoded_bytes);
    store_le32(descriptor + 20, decoded_crc32);
    store_le16(descriptor + 28, (uint16_t)codec);
    store_le16(descriptor + 30, alignment);
}

static int serialize_resource_manifest(
        const pvr_chunk_model_view_t *model, uint8_t **bytes_out,
        size_t *size_out) {
    uint8_t *usage = NULL;
    uint8_t *bytes = NULL;
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    size_t count = 0;
    size_t identifier;
    size_t output_index = 0;
    size_t file_bytes;
    int rv;

    if(!model || !bytes_out || !size_out) {
        errno = EINVAL;
        return -1;
    }
    *bytes_out = NULL;
    *size_out = 0;
    usage = calloc(PVR_CHUNK_TEXTURE_IDENTIFIER_MAX + 1u, sizeof(*usage));
    if(!usage) {
        errno = ENOMEM;
        return -1;
    }
    if(pvr_chunk_polygon_iterator_init(&iterator,
                                       model->model.polygon_words,
                                       model->model.polygon_word_count) < 0)
        goto fail;
    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        uint16_t texture_identifier;
        uint8_t use;

        if(record.record_class != PVR_CHUNK_RECORD_TEXTURE)
            continue;
        if(record.payload_word_count != 1u || !record.payload) {
            errno = EILSEQ;
            goto fail;
        }
        texture_identifier = *(const uint16_t *)record.payload &
                             PVR_CHUNK_TEXTURE_IDENTIFIER_MAX;
        use = record.type == PVR_CHUNK_TEXTURE_TWO_VOLUME ?
              PVR_CHUNK_RESOURCE_SECONDARY :
              PVR_CHUNK_RESOURCE_PRIMARY;
        if(!usage[texture_identifier])
            ++count;
        usage[texture_identifier] |= use;
    }
    if(rv < 0)
        goto fail;
    if(!count) {
        free(usage);
        return 0;
    }
    if(count > (SIZE_MAX - PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES) /
                   PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES) {
        errno = EOVERFLOW;
        goto fail;
    }
    file_bytes = PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES +
                 count * PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES;
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    bytes = calloc(1, file_bytes);
    if(!bytes) {
        errno = ENOMEM;
        goto fail;
    }
    for(identifier = 0;
        identifier <= PVR_CHUNK_TEXTURE_IDENTIFIER_MAX; ++identifier) {
        uint8_t *entry;

        if(!usage[identifier])
            continue;
        entry = bytes + PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES +
                output_index++ * PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES;
        store_le16(entry, (uint16_t)identifier);
        store_le16(entry + 2, usage[identifier]);
    }
    if(output_index != count) {
        errno = EPROTO;
        goto fail;
    }
    store_le32(bytes, PVR_CHUNK_RESOURCE_SECTION_MAGIC);
    store_le16(bytes + 4, PVR_CHUNK_RESOURCE_SECTION_VERSION);
    store_le16(bytes + 6, PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES);
    store_le32(bytes + 8, (uint32_t)file_bytes);
    store_le32(bytes + 12, (uint32_t)count);
    store_le16(bytes + 16, PVR_CHUNK_RESOURCE_SECTION_ENTRY_BYTES);
    store_le32(bytes + 20, (uint32_t)(file_bytes -
                                      PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES));
    store_le32(bytes + 24, crc32_bytes(
        bytes + PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES,
        file_bytes - PVR_CHUNK_RESOURCE_SECTION_HEADER_BYTES));
    store_le32(bytes + 44, crc32_bytes(bytes, 44));
    free(usage);
    *bytes_out = bytes;
    *size_out = file_bytes;
    return 0;

fail:
    {
        int saved_errno = errno ? errno : EIO;

        free(bytes);
        free(usage);
        errno = saved_errno;
    }
    return -1;
}

static int serialize_cooked_cache(const pvr_chunk_model_view_t *view,
                                  uint8_t **bytes_out, size_t *size_out) {
    pvr_chunk_model_plan_requirements_t plan_requirements;
    pvr_chunk_vertex_index_entry_t *entries = NULL;
    pvr_chunk_model_plan_t plan;
    pvr_chunk_cache_requirements_t cache_requirements;
    pvr_chunk_model_cache_t cache;
    void *storage = NULL;
    uint8_t *bytes = NULL;
    size_t serialized_bytes;
    int saved_errno;

    *bytes_out = NULL;
    *size_out = 0;
    if(pvr_chunk_model_plan_query(view, &plan_requirements) < 0)
        goto fail;
    entries = calloc(plan_requirements.vertex_index_entries,
                     sizeof(*entries));
    if(!entries) {
        errno = ENOMEM;
        goto fail;
    }
    if(pvr_chunk_model_plan_build(
           view, entries, plan_requirements.vertex_index_entries,
           &plan) < 0 ||
       pvr_chunk_model_cache_query(&plan, &cache_requirements) < 0)
        goto fail;
    storage = aligned_alloc(PVR_CHUNK_CACHE_ALIGNMENT,
                            cache_requirements.bytes);
    if(!storage) {
        errno = ENOMEM;
        goto fail;
    }
    if(pvr_chunk_model_cache_build(
           &plan, storage, cache_requirements.bytes,
           NULL, NULL, &cache) < 0 ||
       pvr_chunk_model_cache_section_query(
           &cache, &serialized_bytes) < 0)
        goto fail;
    bytes = malloc(serialized_bytes);
    if(!bytes) {
        errno = ENOMEM;
        goto fail;
    }
    if(pvr_chunk_model_cache_section_serialize(
           &cache, bytes, serialized_bytes) < 0)
        goto fail;
    free(storage);
    free(entries);
    *bytes_out = bytes;
    *size_out = serialized_bytes;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    free(bytes);
    free(storage);
    free(entries);
    errno = saved_errno;
    return -1;
}

static int build_asset_blob(const output_streams_t *streams,
                            int lz4_vertices, int section_directory,
                            int include_cooked_cache,
                            const pvr_scene_ir_t *scene,
                            const pvr_chunk_skin_general_t *skin,
                            const pvr_chunk_skeleton_t *skeleton,
                            const pvr_chunk_shape_set_t *shapes,
                            const anim_clip_view_t *animation,
                            const pvr_chunk_morph_animation_t *morph_animation,
                            const void *texture_section,
                            size_t texture_section_bytes,
                            uint8_t **blob_out,
                            size_t *blob_bytes_out) {
    uint8_t *vertex_raw = NULL;
    uint8_t *polygon_raw = NULL;
    uint8_t *vertex_compressed = NULL;
    uint8_t *hierarchy_raw = NULL;
    uint8_t *skin_raw = NULL;
    uint8_t *skeleton_raw = NULL;
    uint8_t *shape_raw = NULL;
    uint8_t *animation_raw = NULL;
    uint8_t *morph_animation_raw = NULL;
    uint8_t *volume_raw = NULL;
    uint8_t *resource_raw = NULL;
    uint8_t *cooked_raw = NULL;
    uint8_t *model_table_raw = NULL;
    const uint8_t *vertex_stored;
    uint8_t *blob = NULL;
    size_t vertex_bytes;
    size_t polygon_bytes;
    size_t vertex_stored_bytes;
    size_t hierarchy_bytes = 0;
    size_t skin_bytes = 0;
    size_t skeleton_bytes = 0;
    size_t shape_bytes = 0;
    size_t animation_bytes = 0;
    size_t morph_animation_bytes = 0;
    size_t volume_bytes = 0;
    size_t resource_bytes = 0;
    size_t cooked_bytes = 0;
    size_t model_table_bytes = 0;
    size_t section_count;
    size_t directory_bytes;
    size_t vertex_offset;
    size_t polygon_offset;
    size_t hierarchy_offset = 0;
    size_t skin_offset = 0;
    size_t skeleton_offset = 0;
    size_t shape_offset = 0;
    size_t animation_offset = 0;
    size_t morph_animation_offset = 0;
    size_t volume_offset = 0;
    size_t resource_offset = 0;
    size_t texture_offset = 0;
    size_t cooked_offset = 0;
    size_t model_table_offset = 0;
    size_t file_bytes;
    int saved_errno;

    if((scene || skin || skeleton || shapes || animation ||
        morph_animation || texture_section) &&
       !section_directory) {
        errno = EINVAL;
        goto fail;
    }
    if((texture_section == NULL) != (texture_section_bytes == 0)) {
        errno = EINVAL;
        goto fail;
    }
    if(serialize_words(streams->vertex_words, streams->vertex_word_count,
                       sizeof(uint32_t), &vertex_raw, &vertex_bytes) < 0 ||
       serialize_words(streams->polygon_words, streams->polygon_word_count,
                       sizeof(uint16_t), &polygon_raw, &polygon_bytes) < 0)
        goto fail;
    if(section_directory) {
        pvr_chunk_model_t source_model = {
            streams->vertex_words, streams->vertex_word_count,
            streams->polygon_words, streams->polygon_word_count,
            { streams->center[0], streams->center[1], streams->center[2] },
            streams->radius
        };
        pvr_chunk_model_view_t source_view;

        if(pvr_chunk_model_open(&source_model, &source_view) < 0 ||
           serialize_resource_manifest(
               &source_view, &resource_raw, &resource_bytes) < 0 ||
           pvr_scene_ir_serialize_volumes(
               &source_view, &volume_raw, &volume_bytes) < 0)
            goto fail;
        if(include_cooked_cache && serialize_cooked_cache(
               &source_view, &cooked_raw, &cooked_bytes) < 0)
            goto fail;
    }
    if(scene && pvr_scene_ir_serialize_hierarchy(
                    scene, &hierarchy_raw, &hierarchy_bytes) < 0)
        goto fail;
    if(skin && pvr_scene_ir_serialize_general_skin(
                   skin, &skin_raw, &skin_bytes) < 0)
        goto fail;
    if(skeleton && pvr_scene_ir_serialize_skeleton(
                       skeleton, &skeleton_raw, &skeleton_bytes) < 0)
        goto fail;
    if(shapes && pvr_scene_ir_serialize_shapes(
                     shapes, &shape_raw, &shape_bytes) < 0)
        goto fail;
    if(animation && pvr_scene_ir_serialize_animation(
                         animation, &animation_raw,
                         &animation_bytes) < 0)
        goto fail;
    if(morph_animation && pvr_scene_ir_serialize_morph_animation(
                               morph_animation, &morph_animation_raw,
                               &morph_animation_bytes) < 0)
        goto fail;
    if(section_directory) {
        pvr_chunk_model_table_record_t record;

        memset(&record, 0, sizeof(record));
        record.resource_ordinal = resource_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record.volume_ordinal = volume_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record.skin4_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
        record.skin_general_ordinal = skin_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record.skeleton_ordinal = skeleton_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record.morph_ordinal = shape_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record.cooked_cache_ordinal = cooked_raw ? 0 :
            PVR_CHUNK_MODEL_SECTION_NONE;
        memcpy(record.center, streams->center, sizeof(record.center));
        record.radius = streams->radius;
        if(pvr_scene_ir_serialize_model_table(
               &record, 1, &model_table_raw, &model_table_bytes) < 0)
            goto fail;
    }
    vertex_stored = vertex_raw;
    vertex_stored_bytes = vertex_bytes;

    if(lz4_vertices) {
        LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
        size_t bound;
        size_t compressed;

        preferences.frameInfo.blockSizeID = LZ4F_max64KB;
        preferences.frameInfo.blockMode = LZ4F_blockIndependent;
        preferences.frameInfo.contentChecksumFlag =
            LZ4F_contentChecksumEnabled;
        preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
        preferences.frameInfo.contentSize = vertex_bytes;
        preferences.autoFlush = 1;
        preferences.favorDecSpeed = 1;
        bound = LZ4F_compressFrameBound(vertex_bytes, &preferences);
        vertex_compressed = malloc(bound);
        if(!vertex_compressed) {
            errno = ENOMEM;
            goto fail;
        }
        compressed = LZ4F_compressFrame(vertex_compressed, bound, vertex_raw,
                                        vertex_bytes, &preferences);
        if(LZ4F_isError(compressed)) {
            errno = EIO;
            goto fail;
        }
        vertex_stored = vertex_compressed;
        vertex_stored_bytes = compressed;
    }

    section_count = 2u + (resource_raw ? 1u : 0u) +
                    (texture_section ? 1u : 0u) +
                    (cooked_raw ? 1u : 0u) +
                    (volume_raw ? 1u : 0u) +
                    (hierarchy_raw ? 1u : 0u) +
                    (skin_raw ? 1u : 0u) +
                    (skeleton_raw ? 1u : 0u) +
                    (shape_raw ? 1u : 0u) +
                    (animation_raw ? 1u : 0u) +
                    (morph_animation_raw ? 1u : 0u) +
                    (model_table_raw ? 1u : 0u);
    if(section_count > SIZE_MAX / PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES) {
        errno = EOVERFLOW;
        goto fail;
    }
    directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    vertex_offset = section_directory ?
        PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + directory_bytes :
        PVR_CHUNK_ASSET_HEADER_BYTES;
    if(vertex_bytes > UINT32_MAX || polygon_bytes > UINT32_MAX ||
       vertex_stored_bytes > UINT32_MAX ||
       vertex_offset > SIZE_MAX - vertex_stored_bytes ||
       align_size_32(vertex_offset + vertex_stored_bytes,
                     &polygon_offset) < 0 ||
       polygon_offset > SIZE_MAX - polygon_bytes) {
        errno = EOVERFLOW;
        goto fail;
    }
    file_bytes = polygon_offset + polygon_bytes;
    if(resource_raw) {
        if(align_size_32(file_bytes, &resource_offset) < 0 ||
           resource_offset > SIZE_MAX - resource_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = resource_offset + resource_bytes;
    }
    if(texture_section) {
        if(align_size_32(file_bytes, &texture_offset) < 0 ||
           texture_offset > SIZE_MAX - texture_section_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = texture_offset + texture_section_bytes;
    }
    if(volume_raw) {
        if(align_size_32(file_bytes, &volume_offset) < 0 ||
           volume_offset > SIZE_MAX - volume_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = volume_offset + volume_bytes;
    }
    if(cooked_raw) {
        if(align_size_32(file_bytes, &cooked_offset) < 0 ||
           cooked_offset > SIZE_MAX - cooked_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = cooked_offset + cooked_bytes;
    }
    if(hierarchy_raw) {
        if(align_size_32(file_bytes, &hierarchy_offset) < 0 ||
           hierarchy_offset > SIZE_MAX - hierarchy_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = hierarchy_offset + hierarchy_bytes;
    }
    if(skin_raw) {
        if(align_size_32(file_bytes, &skin_offset) < 0 ||
           skin_offset > SIZE_MAX - skin_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = skin_offset + skin_bytes;
    }
    if(skeleton_raw) {
        if(align_size_32(file_bytes, &skeleton_offset) < 0 ||
           skeleton_offset > SIZE_MAX - skeleton_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = skeleton_offset + skeleton_bytes;
    }
    if(shape_raw) {
        if(align_size_32(file_bytes, &shape_offset) < 0 ||
           shape_offset > SIZE_MAX - shape_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = shape_offset + shape_bytes;
    }
    if(animation_raw) {
        if(align_size_32(file_bytes, &animation_offset) < 0 ||
           animation_offset > SIZE_MAX - animation_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = animation_offset + animation_bytes;
    }
    if(morph_animation_raw) {
        if(align_size_32(file_bytes, &morph_animation_offset) < 0 ||
           morph_animation_offset > SIZE_MAX - morph_animation_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = morph_animation_offset + morph_animation_bytes;
    }
    if(model_table_raw) {
        if(align_size_32(file_bytes, &model_table_offset) < 0 ||
           model_table_offset > SIZE_MAX - model_table_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = model_table_offset + model_table_bytes;
    }
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }

    blob = calloc(1, file_bytes);
    if(!blob) {
        errno = ENOMEM;
        goto fail;
    }
    memcpy(blob + vertex_offset, vertex_stored,
           vertex_stored_bytes);
    memcpy(blob + polygon_offset, polygon_raw, polygon_bytes);
    if(resource_raw)
        memcpy(blob + resource_offset, resource_raw, resource_bytes);
    if(texture_section)
        memcpy(blob + texture_offset, texture_section,
               texture_section_bytes);
    if(volume_raw)
        memcpy(blob + volume_offset, volume_raw, volume_bytes);
    if(cooked_raw)
        memcpy(blob + cooked_offset, cooked_raw, cooked_bytes);
    if(hierarchy_raw)
        memcpy(blob + hierarchy_offset, hierarchy_raw, hierarchy_bytes);
    if(skin_raw)
        memcpy(blob + skin_offset, skin_raw, skin_bytes);
    if(skeleton_raw)
        memcpy(blob + skeleton_offset, skeleton_raw, skeleton_bytes);
    if(shape_raw)
        memcpy(blob + shape_offset, shape_raw, shape_bytes);
    if(animation_raw)
        memcpy(blob + animation_offset, animation_raw, animation_bytes);
    if(morph_animation_raw)
        memcpy(blob + morph_animation_offset, morph_animation_raw,
               morph_animation_bytes);
    if(model_table_raw)
        memcpy(blob + model_table_offset, model_table_raw,
               model_table_bytes);

    store_le32(blob + 8, (uint32_t)file_bytes);
    store_le32(blob + 16, float_word(streams->center[0]));
    store_le32(blob + 20, float_word(streams->center[1]));
    store_le32(blob + 24, float_word(streams->center[2]));
    store_le32(blob + 28, float_word(streams->radius));
    if(section_directory) {
        uint8_t *directory = blob +
            PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES;
        size_t descriptor_index = 0;

        store_le32(blob, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
        store_le16(blob + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
        store_le16(blob + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
        store_le32(blob + 32, (uint32_t)section_count);
        store_le32(blob + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
        store_le32(blob + 40, (uint32_t)directory_bytes);
        store_pcm2_section(
            directory + descriptor_index++ *
                PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
            PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM,
            vertex_offset, vertex_stored_bytes, vertex_bytes,
            crc32_bytes(vertex_raw, vertex_bytes),
            lz4_vertices ? PVR_CHUNK_ASSET_CODEC_LZ4_FRAME :
                           PVR_CHUNK_ASSET_CODEC_RAW,
            sizeof(uint32_t));
        store_pcm2_section(
            directory + descriptor_index++ *
                PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
            PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM,
            polygon_offset, polygon_bytes, polygon_bytes,
            crc32_bytes(polygon_raw, polygon_bytes),
            PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint16_t));
        if(resource_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE,
                resource_offset, resource_bytes, resource_bytes,
                crc32_bytes(resource_raw, resource_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(texture_section) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_TEXTURE_IMAGES,
                texture_offset, texture_section_bytes,
                texture_section_bytes,
                crc32_bytes(texture_section, texture_section_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, PVR_CHUNK_ASSET_ALIGNMENT);
        }
        if(volume_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_VOLUME_DATA,
                volume_offset, volume_bytes, volume_bytes,
                crc32_bytes(volume_raw, volume_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint16_t));
        }
        if(cooked_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_COOKED_CACHE,
                cooked_offset, cooked_bytes, cooked_bytes,
                crc32_bytes(cooked_raw, cooked_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(hierarchy_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_HIERARCHY,
                hierarchy_offset, hierarchy_bytes, hierarchy_bytes,
                crc32_bytes(hierarchy_raw, hierarchy_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(skin_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL,
                skin_offset, skin_bytes, skin_bytes,
                crc32_bytes(skin_raw, skin_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(skeleton_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_SKELETON,
                skeleton_offset, skeleton_bytes, skeleton_bytes,
                crc32_bytes(skeleton_raw, skeleton_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(shape_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
                shape_offset, shape_bytes, shape_bytes,
                crc32_bytes(shape_raw, shape_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(animation_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_ANIMATION,
                animation_offset, animation_bytes, animation_bytes,
                crc32_bytes(animation_raw, animation_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(morph_animation_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
                morph_animation_offset, morph_animation_bytes,
                morph_animation_bytes,
                crc32_bytes(morph_animation_raw, morph_animation_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(model_table_raw) {
            store_pcm2_section(
                directory + descriptor_index++ *
                    PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES,
                PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
                model_table_offset, model_table_bytes, model_table_bytes,
                crc32_bytes(model_table_raw, model_table_bytes),
                PVR_CHUNK_ASSET_CODEC_RAW, sizeof(uint32_t));
        }
        if(descriptor_index != section_count) {
            errno = EILSEQ;
            goto fail;
        }
        store_le32(blob + 44, crc32_bytes(
            directory, directory_bytes));
        store_le32(blob + 60, crc32_bytes(blob, 60));
    }
    else {
        store_le32(blob, PVR_CHUNK_ASSET_MAGIC);
        store_le16(blob + 4, PVR_CHUNK_ASSET_VERSION);
        store_le16(blob + 6, PVR_CHUNK_ASSET_HEADER_BYTES);
        store_le32(blob + 32, PVR_CHUNK_ASSET_HEADER_BYTES);
        store_le32(blob + 36, (uint32_t)vertex_stored_bytes);
        store_le32(blob + 40, (uint32_t)vertex_bytes);
        store_le32(blob + 44, crc32_bytes(vertex_raw, vertex_bytes));
        store_le16(blob + 48, lz4_vertices ?
            PVR_CHUNK_ASSET_CODEC_LZ4_FRAME : PVR_CHUNK_ASSET_CODEC_RAW);
        store_le32(blob + 56, (uint32_t)polygon_offset);
        store_le32(blob + 60, (uint32_t)polygon_bytes);
        store_le32(blob + 64, (uint32_t)polygon_bytes);
        store_le32(blob + 68, crc32_bytes(polygon_raw, polygon_bytes));
        store_le16(blob + 72, PVR_CHUNK_ASSET_CODEC_RAW);
        store_le32(blob + 80, crc32_bytes(blob, 80));
    }

    free(vertex_compressed);
    free(hierarchy_raw);
    free(skin_raw);
    free(skeleton_raw);
    free(shape_raw);
    free(animation_raw);
    free(morph_animation_raw);
    free(volume_raw);
    free(resource_raw);
    free(cooked_raw);
    free(model_table_raw);
    free(vertex_raw);
    free(polygon_raw);
    *blob_out = blob;
    *blob_bytes_out = file_bytes;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    free(blob);
    free(vertex_compressed);
    free(hierarchy_raw);
    free(skin_raw);
    free(skeleton_raw);
    free(shape_raw);
    free(animation_raw);
    free(morph_animation_raw);
    free(volume_raw);
    free(resource_raw);
    free(cooked_raw);
    free(model_table_raw);
    free(vertex_raw);
    free(polygon_raw);
    errno = saved_errno;
    return -1;
}

typedef struct pcm2_host_section {
    uint32_t type;
    uint8_t *decoded;
    const uint8_t *stored;
    uint8_t *compressed;
    size_t decoded_bytes;
    size_t stored_bytes;
    size_t offset;
    pvr_chunk_asset_codec_t codec;
    uint16_t alignment;
} pcm2_host_section_t;

static void pcm2_host_sections_free(pcm2_host_section_t *sections,
                                    size_t count) {
    size_t index;

    for(index = 0; index < count; ++index) {
        free(sections[index].compressed);
        free(sections[index].decoded);
    }
    free(sections);
}

static int pcm2_host_section_add(pcm2_host_section_t *sections,
                                 size_t capacity, size_t *count,
                                 uint32_t type, uint8_t *decoded,
                                 size_t decoded_bytes,
                                 uint16_t alignment,
                                 int compress) {
    pcm2_host_section_t *section;

    if(*count >= capacity || !decoded || !decoded_bytes) {
        errno = EINVAL;
        return -1;
    }
    section = &sections[(*count)++];
    memset(section, 0, sizeof(*section));
    section->type = type;
    section->decoded = decoded;
    section->stored = decoded;
    section->decoded_bytes = decoded_bytes;
    section->stored_bytes = decoded_bytes;
    section->alignment = alignment;
    section->codec = PVR_CHUNK_ASSET_CODEC_RAW;
    if(compress) {
        LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
        size_t bound;
        size_t compressed_bytes;

        preferences.frameInfo.blockSizeID = LZ4F_max64KB;
        preferences.frameInfo.blockMode = LZ4F_blockIndependent;
        preferences.frameInfo.contentChecksumFlag =
            LZ4F_contentChecksumEnabled;
        preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
        preferences.frameInfo.contentSize = decoded_bytes;
        preferences.autoFlush = 1;
        preferences.favorDecSpeed = 1;
        bound = LZ4F_compressFrameBound(decoded_bytes, &preferences);
        section->compressed = malloc(bound);
        if(!section->compressed) {
            errno = ENOMEM;
            return -1;
        }
        compressed_bytes = LZ4F_compressFrame(
            section->compressed, bound, decoded, decoded_bytes,
            &preferences);
        if(LZ4F_isError(compressed_bytes)) {
            errno = EIO;
            return -1;
        }
        section->stored = section->compressed;
        section->stored_bytes = compressed_bytes;
        section->codec = PVR_CHUNK_ASSET_CODEC_LZ4_FRAME;
    }
    return 0;
}

static int multi_asset_bounds(const output_streams_t *streams,
                              size_t model_count, float center[3],
                              float *radius) {
    double minimum[3];
    double maximum[3];
    double radius_squared = 0.0;
    size_t model;
    size_t component;

    /* The PCM2 header needs one conservative fallback sphere even though
       PMT1 supplies the exact local sphere used after table admission. */
    for(component = 0; component < 3; ++component) {
        minimum[component] = (double)streams[0].center[component] -
                             streams[0].radius;
        maximum[component] = (double)streams[0].center[component] +
                             streams[0].radius;
    }
    for(model = 1; model < model_count; ++model) {
        for(component = 0; component < 3; ++component) {
            double low = (double)streams[model].center[component] -
                         streams[model].radius;
            double high = (double)streams[model].center[component] +
                          streams[model].radius;

            if(low < minimum[component])
                minimum[component] = low;
            if(high > maximum[component])
                maximum[component] = high;
        }
    }
    for(component = 0; component < 3; ++component) {
        double half = (maximum[component] - minimum[component]) * 0.5;
        double value = minimum[component] + half;

        if(!isfinite(value) || fabs(value) > FLT_MAX ||
           !isfinite(half)) {
            errno = EOVERFLOW;
            return -1;
        }
        center[component] = (float)value;
        radius_squared += half * half;
    }
    if(!isfinite(radius_squared) || sqrt(radius_squared) > FLT_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    *radius = (float)sqrt(radius_squared);
    return 0;
}

static int build_multi_asset_blob(const output_streams_t *streams,
                                  size_t model_count, int lz4_vertices,
                                  int include_cooked_cache,
                                  const pvr_scene_ir_t *scene,
                                  const gltf_model_metadata_t *metadata,
                                  const gltf_animation_metadata_t *animations,
                                  size_t animation_count,
                                  const void *texture_section,
                                  size_t texture_section_bytes,
                                  uint8_t **blob_out,
                                  size_t *blob_bytes_out) {
    pcm2_host_section_t *sections = NULL;
    pvr_chunk_model_table_record_t *records = NULL;
    pvr_scene_ir_animation_clip_t *catalog_clips = NULL;
    uint8_t *blob = NULL;
    size_t section_capacity;
    size_t section_count = 0;
    size_t resource_ordinal = 0;
    size_t cooked_ordinal = 0;
    size_t skin_ordinal = 0;
    size_t skeleton_ordinal = 0;
    size_t morph_ordinal = 0;
    size_t directory_bytes;
    size_t file_bytes;
    float center[3];
    float radius;
    size_t model;
    size_t index;
    int saved_errno;

    if(blob_out)
        *blob_out = NULL;
    if(blob_bytes_out)
        *blob_bytes_out = 0;
    if(!streams || !model_count || !scene || !blob_out ||
       !blob_bytes_out || model_count > UINT32_MAX ||
       !metadata || (animation_count && !animations) ||
       animation_count > UINT32_MAX ||
       model_count > (SIZE_MAX - 4u) / 7u ||
       ((texture_section == NULL) != (texture_section_bytes == 0))) {
        errno = EINVAL;
        return -1;
    }
    section_capacity = model_count * 7u + 4u;
    if(animation_count > (SIZE_MAX - section_capacity) / 2u) {
        errno = EOVERFLOW;
        return -1;
    }
    section_capacity += animation_count * 2u;
    sections = calloc(section_capacity, sizeof(*sections));
    records = calloc(model_count, sizeof(*records));
    catalog_clips = animation_count ?
        calloc(animation_count, sizeof(*catalog_clips)) : NULL;
    if(!sections || !records || (animation_count && !catalog_clips)) {
        errno = ENOMEM;
        goto fail;
    }
    for(model = 0; model < model_count; ++model) {
        pvr_chunk_model_t source_model = {
            streams[model].vertex_words,
            streams[model].vertex_word_count,
            streams[model].polygon_words,
            streams[model].polygon_word_count,
            { streams[model].center[0], streams[model].center[1],
              streams[model].center[2] },
            streams[model].radius
        };
        pvr_chunk_model_view_t source_view;
        uint8_t *vertex = NULL;
        uint8_t *polygon = NULL;
        uint8_t *resource = NULL;
        uint8_t *cooked = NULL;
        uint8_t *skin = NULL;
        uint8_t *skeleton = NULL;
        uint8_t *morph = NULL;
        size_t vertex_bytes = 0;
        size_t polygon_bytes = 0;
        size_t resource_bytes = 0;
        size_t cooked_bytes = 0;
        size_t skin_bytes = 0;
        size_t skeleton_bytes = 0;
        size_t morph_bytes = 0;
        const gltf_model_metadata_t *model_metadata = &metadata[model];
        pvr_chunk_model_table_record_t *record = &records[model];

        /* Interleaving each pair with its optional manifest keeps file data
           sequential; model pairing remains ordinal-by-section-type. */
        if(serialize_words(
               streams[model].vertex_words,
               streams[model].vertex_word_count, sizeof(uint32_t),
               &vertex, &vertex_bytes) < 0 ||
           pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_VERTEX_STREAM, vertex,
               vertex_bytes, sizeof(uint32_t), lz4_vertices) < 0)
            goto fail;
        vertex = NULL;
        if(serialize_words(
               streams[model].polygon_words,
               streams[model].polygon_word_count, sizeof(uint16_t),
               &polygon, &polygon_bytes) < 0 ||
           pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_POLYGON_STREAM, polygon,
               polygon_bytes, sizeof(uint16_t), 0) < 0)
            goto fail;
        polygon = NULL;
        if(pvr_chunk_model_open(&source_model, &source_view) < 0 ||
           serialize_resource_manifest(
               &source_view, &resource, &resource_bytes) < 0)
            goto fail;
        if(include_cooked_cache && serialize_cooked_cache(
               &source_view, &cooked, &cooked_bytes) < 0)
            goto fail;
        if(model_metadata->skin.span_count &&
           pvr_scene_ir_serialize_general_skin(
               &model_metadata->skin, &skin, &skin_bytes) < 0)
            goto fail;
        if(model_metadata->skeleton.joint_count &&
           pvr_scene_ir_serialize_skeleton(
               &model_metadata->skeleton, &skeleton,
               &skeleton_bytes) < 0)
            goto fail;
        if(model_metadata->shapes.target_count &&
           pvr_scene_ir_serialize_shapes(
               &model_metadata->shapes, &morph, &morph_bytes) < 0)
            goto fail;

        memset(record, 0, sizeof(*record));
        record->vertex_ordinal = model;
        record->polygon_ordinal = model;
        record->resource_ordinal = resource ? resource_ordinal++ :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record->volume_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
        record->skin4_ordinal = PVR_CHUNK_MODEL_SECTION_NONE;
        record->skin_general_ordinal = skin ? skin_ordinal++ :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record->skeleton_ordinal = skeleton ? skeleton_ordinal++ :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record->morph_ordinal = morph ? morph_ordinal++ :
            PVR_CHUNK_MODEL_SECTION_NONE;
        record->cooked_cache_ordinal = cooked ? cooked_ordinal++ :
            PVR_CHUNK_MODEL_SECTION_NONE;
        memcpy(record->center, streams[model].center,
               sizeof(record->center));
        record->radius = streams[model].radius;
        if(resource && pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE, resource,
               resource_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
        if(cooked && pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_COOKED_CACHE, cooked,
               cooked_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
        if(skin && pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL, skin,
               skin_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
        if(skeleton && pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_SKELETON, skeleton,
               skeleton_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
        if(morph && pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS, morph,
               morph_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
    }
    {
        uint8_t *hierarchy = NULL;
        size_t hierarchy_bytes = 0;

        if(pvr_scene_ir_serialize_hierarchy(
               scene, &hierarchy, &hierarchy_bytes) < 0 ||
           pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_HIERARCHY, hierarchy,
               hierarchy_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
    }
    if(animation_count) {
        uint32_t transform_ordinal = 0;
        uint32_t morph_animation_ordinal = 0;
        size_t animation;

        for(animation = 0; animation < animation_count; ++animation) {
            const gltf_animation_metadata_t *source =
                &animations[animation];
            pvr_scene_ir_animation_clip_t *catalog =
                &catalog_clips[animation];

            catalog->name = source->name;
            catalog->transform_ordinal =
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
            catalog->morph_ordinal =
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
            catalog->start_time = FLT_MAX;
            catalog->end_time = -FLT_MAX;
            if(source->animation_track_count) {
                uint8_t *data = NULL;
                size_t bytes = 0;

                if(pvr_scene_ir_serialize_animation(
                       &source->animation, &data, &bytes) < 0)
                    goto fail;
                if(pcm2_host_section_add(
                       sections, section_capacity, &section_count,
                       PVR_CHUNK_ASSET_SECTION_ANIMATION, data, bytes,
                       sizeof(uint32_t), 0) < 0) {
                    free(data);
                    goto fail;
                }
                catalog->transform_ordinal = transform_ordinal++;
                catalog->start_time = source->animation.clip.start_time;
                catalog->end_time = source->animation.clip.end_time;
            }
            if(source->morph_animation.binding_count) {
                uint8_t *data = NULL;
                size_t bytes = 0;

                if(pvr_scene_ir_serialize_morph_animation(
                       &source->morph_animation, &data, &bytes) < 0)
                    goto fail;
                if(pcm2_host_section_add(
                       sections, section_capacity, &section_count,
                       PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
                       data, bytes, sizeof(uint32_t), 0) < 0) {
                    free(data);
                    goto fail;
                }
                catalog->morph_ordinal = morph_animation_ordinal++;
                if(source->morph_animation.start_time <
                   catalog->start_time)
                    catalog->start_time =
                        source->morph_animation.start_time;
                if(source->morph_animation.end_time > catalog->end_time)
                    catalog->end_time = source->morph_animation.end_time;
            }
        }
        {
            uint8_t *catalog = NULL;
            size_t catalog_bytes = 0;

            if(pvr_scene_ir_serialize_animation_catalog(
                   catalog_clips, animation_count, &catalog,
                   &catalog_bytes) < 0)
                goto fail;
            if(pcm2_host_section_add(
                   sections, section_capacity, &section_count,
                   PVR_CHUNK_ASSET_SECTION_ANIMATION_CATALOG,
                   catalog, catalog_bytes, sizeof(uint32_t), 0) < 0) {
                free(catalog);
                goto fail;
            }
        }
    }
    {
        uint8_t *table = NULL;
        size_t table_bytes = 0;

        if(pvr_scene_ir_serialize_model_table(
               records, model_count, &table, &table_bytes) < 0 ||
           pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_MODEL_TABLE, table,
               table_bytes, sizeof(uint32_t), 0) < 0)
            goto fail;
    }
    if(texture_section) {
        uint8_t *texture_copy = malloc(texture_section_bytes);

        if(!texture_copy) {
            errno = ENOMEM;
            goto fail;
        }
        memcpy(texture_copy, texture_section, texture_section_bytes);
        if(pcm2_host_section_add(
               sections, section_capacity, &section_count,
               PVR_CHUNK_ASSET_SECTION_TEXTURE_IMAGES, texture_copy,
               texture_section_bytes, PVR_CHUNK_ASSET_ALIGNMENT, 0) < 0) {
            free(texture_copy);
            goto fail;
        }
    }
    if(section_count > UINT32_MAX ||
       section_count > SIZE_MAX /
           PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES ||
       multi_asset_bounds(streams, model_count, center, &radius) < 0)
        goto fail;
    directory_bytes = section_count *
        PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;
    file_bytes = PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES + directory_bytes;
    for(index = 0; index < section_count; ++index) {
        if(align_size_32(file_bytes, &sections[index].offset) < 0 ||
           sections[index].offset > SIZE_MAX -
               sections[index].stored_bytes) {
            errno = EOVERFLOW;
            goto fail;
        }
        file_bytes = sections[index].offset + sections[index].stored_bytes;
    }
    if(file_bytes > UINT32_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    blob = calloc(1, file_bytes);
    if(!blob) {
        errno = ENOMEM;
        goto fail;
    }
    store_le32(blob, PVR_CHUNK_ASSET_DIRECTORY_MAGIC);
    store_le16(blob + 4, PVR_CHUNK_ASSET_DIRECTORY_VERSION);
    store_le16(blob + 6, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    store_le32(blob + 8, (uint32_t)file_bytes);
    store_le32(blob + 16, float_word(center[0]));
    store_le32(blob + 20, float_word(center[1]));
    store_le32(blob + 24, float_word(center[2]));
    store_le32(blob + 28, float_word(radius));
    store_le32(blob + 32, (uint32_t)section_count);
    store_le32(blob + 36, PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES);
    store_le32(blob + 40, (uint32_t)directory_bytes);
    for(index = 0; index < section_count; ++index) {
        pcm2_host_section_t *section = &sections[index];
        uint8_t *descriptor = blob +
            PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES +
            index * PVR_CHUNK_ASSET_DIRECTORY_ENTRY_BYTES;

        memcpy(blob + section->offset, section->stored,
               section->stored_bytes);
        store_pcm2_section(
            descriptor, section->type, section->offset,
            section->stored_bytes, section->decoded_bytes,
            crc32_bytes(section->decoded, section->decoded_bytes),
            section->codec, section->alignment);
    }
    store_le32(blob + 44, crc32_bytes(
        blob + PVR_CHUNK_ASSET_DIRECTORY_HEADER_BYTES,
        directory_bytes));
    store_le32(blob + 60, crc32_bytes(blob, 60));
    free(records);
    free(catalog_clips);
    pcm2_host_sections_free(sections, section_count);
    *blob_out = blob;
    *blob_bytes_out = file_bytes;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    free(blob);
    free(records);
    free(catalog_clips);
    pcm2_host_sections_free(sections, section_count);
    errno = saved_errno;
    return -1;
}

static int load_raw_section_ordinal(
    const pvr_chunk_asset_view_t *view, uint32_t type,
    size_t ordinal,
    pvr_chunk_asset_section_t *section, const void **decoded) {
    size_t index;
    size_t found = 0;

    if(decoded)
        *decoded = NULL;
    if(!view || !section || !decoded) {
        errno = EINVAL;
        return -1;
    }
    for(index = 0; index < view->section_count; ++index) {
        if(pvr_chunk_asset_section_get(view, index, section) < 0)
            return -1;
        if(section->type != type)
            continue;
        if(found++ == ordinal)
            return pvr_chunk_asset_section_load(
                view, index, NULL, NULL, NULL, 0, decoded);
    }
    errno = ENOENT;
    return -1;
}

static int load_raw_section_type(
    const pvr_chunk_asset_view_t *view, uint32_t type,
    pvr_chunk_asset_section_t *section, const void **decoded) {
    return load_raw_section_ordinal(
        view, type, 0, section, decoded);
}

static int prepare_blob_output(const char *target, const void *data,
                               size_t size,
                               temporary_output_t *temporary) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t target_length = strlen(target);
    FILE *file = NULL;
    int descriptor = -1;
    int saved_errno;

    memset(temporary, 0, sizeof(*temporary));
    if(target_length > SIZE_MAX - sizeof(suffix)) {
        errno = EOVERFLOW;
        return -1;
    }
    temporary->path = malloc(target_length + sizeof(suffix));
    if(!temporary->path) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(temporary->path, target, target_length);
    memcpy(temporary->path + target_length, suffix, sizeof(suffix));
    descriptor = mkstemp(temporary->path);
    if(descriptor < 0)
        goto fail;
    file = fdopen(descriptor, "wb");
    if(!file)
        goto fail;
    descriptor = -1;
    if(fwrite(data, 1, size, file) != size || fflush(file) < 0)
        goto fail;
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    else if(descriptor >= 0)
        close(descriptor);
    if(temporary->path)
        unlink(temporary->path);
    free(temporary->path);
    temporary->path = NULL;
    errno = saved_errno;
    return -1;
}

static int prepare_asset_output(const char *target,
                                const output_streams_t *streams,
                                int lz4_vertices, int section_directory,
                                int include_cooked_cache,
                                const pvr_scene_ir_t *scene,
                                const pvr_chunk_skin_general_t *skin,
                                const pvr_chunk_skeleton_t *skeleton,
                                const pvr_chunk_shape_set_t *shapes,
                                const anim_clip_view_t *animation,
                                const pvr_chunk_morph_animation_t *
                                    morph_animation,
                                const void *texture_section,
                                size_t texture_section_bytes,
                                temporary_output_t *temporary,
                                size_t *asset_bytes) {
    uint8_t *blob = NULL;
    size_t blob_bytes = 0;
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_workspace_requirements_t requirements;
    pvr_chunk_asset_section_t hierarchy_section;
    pvr_chunk_asset_section_t skin_section;
    pvr_chunk_asset_section_t skeleton_section;
    pvr_chunk_asset_section_t shape_section;
    pvr_chunk_asset_section_t animation_section;
    pvr_chunk_asset_section_t morph_animation_section;
    pvr_chunk_asset_section_t volume_section;
    pvr_chunk_asset_section_t resource_section;
    pvr_chunk_asset_section_t texture_image_section;
    pvr_chunk_asset_section_t cooked_section;
    pvr_chunk_asset_section_t model_table_section;
    pvr_chunk_model_view_t model_view;
    pvr_chunk_scene_hierarchy_view_t hierarchy_view;
    pvr_chunk_skin_general_section_view_t skin_view;
    pvr_chunk_skin_general_t materialized_skin;
    pvr_chunk_skeleton_section_view_t skeleton_view;
    pvr_chunk_skeleton_t materialized_skeleton;
    pvr_chunk_shape_section_view_t shape_view;
    pvr_chunk_shape_set_t materialized_shapes;
    pvr_chunk_animation_section_view_t animation_view;
    pvr_chunk_morph_animation_section_view_t morph_animation_view;
    pvr_chunk_volume_section_view_t volume_view;
    pvr_chunk_resource_section_view_t resource_view;
    pvr_chunk_texture_section_view_t texture_image_view;
    pvr_chunk_cache_section_view_t cooked_view;
    pvr_chunk_model_table_view_t model_table_view;
    pvr_chunk_cache_section_requirements_t cooked_requirements;
    pvr_chunk_model_cache_t cooked_cache;
    anim_clip_view_t materialized_animation;
    pvr_chunk_morph_animation_t materialized_morph_animation;
    pvr_chunk_hierarchy_t hierarchy;
    pvr_chunk_hierarchy_node_t *hierarchy_nodes = NULL;
    pvr_chunk_skin_span_t *skin_spans = NULL;
    pvr_chunk_skin_weight_t *skin_weights = NULL;
    pvr_chunk_skeleton_joint_t *skeleton_joints = NULL;
    pvr_chunk_shape_target_t *shape_targets = NULL;
    pvr_chunk_shape_delta_t *shape_deltas = NULL;
    pvr_chunk_animation_key_t *animation_keys = NULL;
    anim_track_view_t *animation_tracks = NULL;
    anim_transform_tracks_t *animation_transforms = NULL;
    anim_visibility_tracks_t *animation_visibility = NULL;
    anim_scalar_hermite_key_t *morph_animation_keys = NULL;
    anim_track_view_t *morph_animation_tracks = NULL;
    pvr_chunk_shape_channel_t *morph_animation_channels = NULL;
    pvr_chunk_morph_animation_binding_t *morph_animation_bindings = NULL;
    const pvr_chunk_model_view_t *models[1];
    const void *hierarchy_data = NULL;
    const void *skin_data = NULL;
    const void *skeleton_data = NULL;
    const void *shape_data = NULL;
    const void *animation_data = NULL;
    const void *morph_animation_data = NULL;
    const void *volume_data = NULL;
    const void *resource_data = NULL;
    const void *texture_image_data = NULL;
    const void *cooked_data = NULL;
    const void *model_table_data = NULL;
    void *workspace = NULL;
    void *cooked_storage = NULL;
    size_t workspace_allocation = 0;
    int result = -1;

    if(build_asset_blob(streams, lz4_vertices, section_directory,
                        include_cooked_cache, scene, skin, skeleton, shapes,
                        animation, morph_animation, texture_section,
                        texture_section_bytes,
                        &blob, &blob_bytes) < 0 ||
       pvr_chunk_asset_open(blob, blob_bytes, &asset_view) < 0 ||
       pvr_chunk_asset_workspace_query(&asset_view, &requirements) < 0)
        goto out;
    if(requirements.bytes) {
        if(align_size_32(requirements.bytes, &workspace_allocation) < 0)
            goto out;
        workspace = aligned_alloc(PVR_CHUNK_ASSET_ALIGNMENT,
                                  workspace_allocation);
        if(!workspace) {
            errno = ENOMEM;
            goto out;
        }
    }
    if(pvr_chunk_asset_load(&asset_view, pvr_chunk_asset_lz4_decode, NULL,
                            workspace, workspace_allocation,
                            &model_view) < 0)
        goto out;
    if(section_directory) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_MODEL_TABLE,
               &model_table_section, &model_table_data) < 0 ||
           pvr_chunk_model_table_open(
               model_table_data, model_table_section.decoded_bytes,
               &model_table_view) < 0 ||
           pvr_chunk_model_table_validate_asset(
               &model_table_view, &asset_view) < 0 ||
           pvr_chunk_model_table_load(
               &model_table_view, &asset_view, 0,
               pvr_chunk_asset_lz4_decode, NULL, workspace,
               workspace_allocation, &model_view) < 0)
            goto out;
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE,
               &resource_section, &resource_data) == 0) {
            if(pvr_chunk_resource_section_open(
                   resource_data, resource_section.decoded_bytes,
                   &resource_view) < 0 ||
               pvr_chunk_resource_section_validate_model(
                   &resource_view, &model_view) < 0)
                goto out;
        }
        else if(errno != ENOENT)
            goto out;
        if(texture_section &&
           (load_raw_section_type(
                &asset_view, PVR_CHUNK_ASSET_SECTION_TEXTURE_IMAGES,
                &texture_image_section, &texture_image_data) < 0 ||
            pvr_chunk_texture_section_open(
                texture_image_data, texture_image_section.decoded_bytes,
                &texture_image_view) < 0 ||
            texture_image_view.entry_count == 0)) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        if(include_cooked_cache) {
            if(load_raw_section_type(
                   &asset_view, PVR_CHUNK_ASSET_SECTION_COOKED_CACHE,
                   &cooked_section, &cooked_data) < 0 ||
               pvr_chunk_cache_section_open(
                   cooked_data, cooked_section.decoded_bytes,
                   &cooked_view) < 0 ||
               pvr_chunk_cache_section_workspace_query(
                   &cooked_view, &cooked_requirements) < 0)
                goto out;
            cooked_storage = aligned_alloc(
                cooked_requirements.alignment,
                cooked_requirements.bytes);
            if(!cooked_storage) {
                errno = ENOMEM;
                goto out;
            }
            if(pvr_chunk_cache_section_materialize_ordinary(
                   &cooked_view, cooked_storage,
                   cooked_requirements.bytes, &cooked_cache) < 0 ||
               cooked_cache.vertex_count !=
                   model_view.info.index_references ||
               cooked_cache.strip_count != model_view.info.strips) {
                if(!errno)
                    errno = EILSEQ;
                goto out;
            }
        }
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_VOLUME_DATA,
               &volume_section, &volume_data) == 0) {
            if(pvr_chunk_volume_section_open(
                   volume_data, volume_section.decoded_bytes,
                   &volume_view) < 0 ||
               pvr_chunk_volume_section_validate_model(
                   &volume_view, &model_view) < 0)
                goto out;
        }
        else if(errno != ENOENT)
            goto out;
    }
    if(scene) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_HIERARCHY,
               &hierarchy_section, &hierarchy_data) < 0 ||
           pvr_chunk_scene_hierarchy_open(
               hierarchy_data, hierarchy_section.decoded_bytes,
               &hierarchy_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        if(hierarchy_view.node_count) {
            hierarchy_nodes = calloc(hierarchy_view.node_count,
                                     sizeof(*hierarchy_nodes));
            if(!hierarchy_nodes) {
                errno = ENOMEM;
                goto out;
            }
        }
        models[0] = &model_view;
        if(pvr_chunk_scene_hierarchy_bind(
               &hierarchy_view, models, 1, hierarchy_nodes,
               hierarchy_view.node_count, &hierarchy) < 0)
            goto out;
    }
    if(skin) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL,
               &skin_section, &skin_data) < 0 ||
           pvr_chunk_skin_general_section_open(
               skin_data, skin_section.decoded_bytes, &skin_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        skin_spans = calloc(skin_view.span_count, sizeof(*skin_spans));
        skin_weights = calloc(skin_view.weight_count, sizeof(*skin_weights));
        if(!skin_spans || !skin_weights) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_skin_general_section_materialize(
               &skin_view, skin_spans, skin_view.span_count,
               skin_weights, skin_view.weight_count,
               &materialized_skin) < 0 ||
           materialized_skin.span_count != skin->span_count ||
           materialized_skin.weight_count != skin->weight_count ||
           materialized_skin.joint_count != skin->joint_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(skeleton) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_SKELETON,
               &skeleton_section, &skeleton_data) < 0 ||
           pvr_chunk_skeleton_section_open(
               skeleton_data, skeleton_section.decoded_bytes,
               &skeleton_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        skeleton_joints = calloc(skeleton_view.joint_count,
                                 sizeof(*skeleton_joints));
        if(!skeleton_joints) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_skeleton_section_materialize(
               &skeleton_view, skeleton_joints,
               skeleton_view.joint_count, &materialized_skeleton) < 0 ||
           materialized_skeleton.joint_count != skeleton->joint_count ||
           materialized_skeleton.node_count != skeleton->node_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(shapes) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
               &shape_section, &shape_data) < 0 ||
           pvr_chunk_shape_section_open(
               shape_data, shape_section.decoded_bytes, &shape_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        shape_targets = calloc(shape_view.target_count,
                               sizeof(*shape_targets));
        shape_deltas = calloc(shape_view.delta_count,
                              sizeof(*shape_deltas));
        if(!shape_targets || !shape_deltas) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_shape_section_materialize(
               &shape_view, shape_targets, shape_view.target_count,
               shape_deltas, shape_view.delta_count,
               &materialized_shapes) < 0 ||
           materialized_shapes.target_count != shapes->target_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(animation) {
        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_ANIMATION,
               &animation_section, &animation_data) < 0 ||
           pvr_chunk_animation_section_open(
               animation_data, animation_section.decoded_bytes,
               &animation_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        animation_keys = calloc(animation_view.key_count,
                                sizeof(*animation_keys));
        animation_tracks = calloc(animation_view.track_count,
                                  sizeof(*animation_tracks));
        animation_transforms = calloc(animation_view.transform_count,
                                      sizeof(*animation_transforms));
        animation_visibility = calloc(animation_view.transform_count,
                                      sizeof(*animation_visibility));
        if((animation_view.key_count && !animation_keys) ||
           (animation_view.track_count && !animation_tracks) ||
           !animation_transforms || !animation_visibility) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_animation_section_materialize(
               &animation_view, animation_keys, animation_view.key_count,
               animation_tracks, animation_view.track_count,
               animation_transforms, animation_view.transform_count,
               animation_visibility, animation_view.transform_count,
               &materialized_animation) < 0 ||
           materialized_animation.clip.transform_count !=
               animation->clip.transform_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(morph_animation) {
        const pvr_chunk_shape_section_view_t *shape_views[1];

        if(!scene || !shapes ||
           load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
               &morph_animation_section, &morph_animation_data) < 0 ||
           pvr_chunk_morph_animation_section_open(
               morph_animation_data,
               morph_animation_section.decoded_bytes,
               &morph_animation_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        shape_views[0] = &shape_view;
        if(pvr_chunk_morph_animation_section_validate_scene(
               &morph_animation_view, &hierarchy_view,
               &model_table_view, shape_views, 1) < 0)
            goto out;
        morph_animation_keys = calloc(
            morph_animation_view.key_count,
            sizeof(*morph_animation_keys));
        morph_animation_tracks = calloc(
            morph_animation_view.track_count,
            sizeof(*morph_animation_tracks));
        morph_animation_channels = calloc(
            morph_animation_view.channel_count,
            sizeof(*morph_animation_channels));
        morph_animation_bindings = calloc(
            morph_animation_view.binding_count,
            sizeof(*morph_animation_bindings));
        if(!morph_animation_keys || !morph_animation_tracks ||
           !morph_animation_channels || !morph_animation_bindings) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_morph_animation_section_materialize(
               &morph_animation_view, morph_animation_keys,
               morph_animation_view.key_count, morph_animation_tracks,
               morph_animation_view.track_count,
               morph_animation_channels,
               morph_animation_view.channel_count,
               morph_animation_bindings,
               morph_animation_view.binding_count,
               &materialized_morph_animation) < 0 ||
           materialized_morph_animation.binding_count !=
               morph_animation->binding_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(prepare_blob_output(target, blob, blob_bytes, temporary) < 0)
        goto out;
    *asset_bytes = blob_bytes;
    result = 0;

out:
    free(morph_animation_bindings);
    free(morph_animation_channels);
    free(morph_animation_tracks);
    free(morph_animation_keys);
    free(skeleton_joints);
    free(cooked_storage);
    free(animation_visibility);
    free(animation_transforms);
    free(animation_tracks);
    free(animation_keys);
    free(shape_deltas);
    free(shape_targets);
    free(skin_weights);
    free(skin_spans);
    free(hierarchy_nodes);
    free(workspace);
    free(blob);
    return result;
}

static int validate_multi_model_metadata(
    const pvr_chunk_asset_view_t *asset,
    const pvr_chunk_model_table_record_t *record,
    const gltf_model_metadata_t *expected) {
    pvr_chunk_skin_span_t *skin_spans = NULL;
    pvr_chunk_skin_weight_t *skin_weights = NULL;
    pvr_chunk_skeleton_joint_t *skeleton_joints = NULL;
    pvr_chunk_shape_target_t *shape_targets = NULL;
    pvr_chunk_shape_delta_t *shape_deltas = NULL;
    int result = -1;

    if((record->skin_general_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) !=
           (expected->skin.span_count != 0) ||
       (record->skeleton_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) !=
           (expected->skeleton.joint_count != 0) ||
       (record->morph_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) !=
           (expected->shapes.target_count != 0)) {
        errno = EILSEQ;
        goto out;
    }
    if(expected->skin.span_count) {
        pvr_chunk_asset_section_t section;
        pvr_chunk_skin_general_section_view_t view;
        pvr_chunk_skin_general_t materialized;
        const void *data;

        if(load_raw_section_ordinal(
               asset, PVR_CHUNK_ASSET_SECTION_SKIN_GENERAL,
               record->skin_general_ordinal, &section, &data) < 0 ||
           pvr_chunk_skin_general_section_open(
               data, section.decoded_bytes, &view) < 0)
            goto out;
        skin_spans = calloc(view.span_count, sizeof(*skin_spans));
        skin_weights = calloc(view.weight_count, sizeof(*skin_weights));
        if(!skin_spans || !skin_weights) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_skin_general_section_materialize(
               &view, skin_spans, view.span_count, skin_weights,
               view.weight_count, &materialized) < 0 ||
           materialized.span_count != expected->skin.span_count ||
           materialized.weight_count != expected->skin.weight_count ||
           materialized.joint_count != expected->skin.joint_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(expected->skeleton.joint_count) {
        pvr_chunk_asset_section_t section;
        pvr_chunk_skeleton_section_view_t view;
        pvr_chunk_skeleton_t materialized;
        const void *data;

        if(load_raw_section_ordinal(
               asset, PVR_CHUNK_ASSET_SECTION_SKELETON,
               record->skeleton_ordinal, &section, &data) < 0 ||
           pvr_chunk_skeleton_section_open(
               data, section.decoded_bytes, &view) < 0)
            goto out;
        skeleton_joints = calloc(view.joint_count,
                                 sizeof(*skeleton_joints));
        if(!skeleton_joints) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_skeleton_section_materialize(
               &view, skeleton_joints, view.joint_count,
               &materialized) < 0 ||
           materialized.joint_count != expected->skeleton.joint_count ||
           materialized.node_count != expected->skeleton.node_count) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    if(expected->shapes.target_count) {
        pvr_chunk_asset_section_t section;
        pvr_chunk_shape_section_view_t view;
        pvr_chunk_shape_set_t materialized;
        const void *data;
        size_t expected_deltas = 0;
        size_t target;

        for(target = 0; target < expected->shapes.target_count; ++target) {
            if(expected_deltas > SIZE_MAX -
                   expected->shapes.targets[target].delta_count) {
                errno = EOVERFLOW;
                goto out;
            }
            expected_deltas +=
                expected->shapes.targets[target].delta_count;
        }

        if(load_raw_section_ordinal(
               asset, PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
               record->morph_ordinal, &section, &data) < 0 ||
           pvr_chunk_shape_section_open(
               data, section.decoded_bytes, &view) < 0)
            goto out;
        shape_targets = calloc(view.target_count, sizeof(*shape_targets));
        shape_deltas = calloc(view.delta_count, sizeof(*shape_deltas));
        if(!shape_targets || !shape_deltas) {
            errno = ENOMEM;
            goto out;
        }
        if(pvr_chunk_shape_section_materialize(
               &view, shape_targets, view.target_count, shape_deltas,
               view.delta_count, &materialized) < 0 ||
           materialized.target_count != expected->shapes.target_count ||
           view.delta_count != expected_deltas) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
    }
    result = 0;

out:
    free(shape_deltas);
    free(shape_targets);
    free(skeleton_joints);
    free(skin_weights);
    free(skin_spans);
    return result;
}

static int validate_multi_morph_animation(
    const pvr_chunk_asset_view_t *asset, size_t ordinal,
    const pvr_chunk_scene_hierarchy_view_t *hierarchy,
    const pvr_chunk_model_table_view_t *table,
    const pvr_chunk_shape_section_view_t *const *shape_views,
    size_t shape_view_count,
    const pvr_chunk_morph_animation_t *expected) {
    pvr_chunk_asset_section_t section;
    pvr_chunk_morph_animation_section_view_t view;
    const void *data;
    anim_scalar_hermite_key_t *keys = NULL;
    anim_track_view_t *tracks = NULL;
    pvr_chunk_shape_channel_t *channels = NULL;
    pvr_chunk_morph_animation_binding_t *bindings = NULL;
    pvr_chunk_morph_animation_t materialized;
    int result = -1;

    if(load_raw_section_ordinal(
           asset, PVR_CHUNK_ASSET_SECTION_MORPH_ANIMATION,
           ordinal, &section, &data) < 0 ||
       pvr_chunk_morph_animation_section_open(
           data, section.decoded_bytes, &view) < 0 ||
       pvr_chunk_morph_animation_section_validate_scene(
           &view, hierarchy, table, shape_views,
           shape_view_count) < 0)
        goto out;
    keys = calloc(view.key_count, sizeof(*keys));
    tracks = calloc(view.track_count, sizeof(*tracks));
    channels = calloc(view.channel_count, sizeof(*channels));
    bindings = calloc(view.binding_count, sizeof(*bindings));
    if(!keys || !tracks || !channels || !bindings) {
        errno = ENOMEM;
        goto out;
    }
    if(pvr_chunk_morph_animation_section_materialize(
           &view, keys, view.key_count, tracks, view.track_count,
           channels, view.channel_count, bindings, view.binding_count,
           &materialized) < 0 ||
       materialized.binding_count != expected->binding_count) {
        if(!errno)
            errno = EILSEQ;
        goto out;
    }
    result = 0;

out:
    free(bindings);
    free(channels);
    free(tracks);
    free(keys);
    return result;
}

static int prepare_multi_asset_output(
    const char *target, const output_streams_t *streams,
    size_t model_count, int lz4_vertices, int include_cooked_cache,
    const pvr_scene_ir_t *scene,
    const gltf_model_metadata_t *metadata,
    const gltf_animation_metadata_t *animations,
    size_t animation_count,
    const void *texture_section, size_t texture_section_bytes,
    temporary_output_t *temporary,
    size_t *asset_bytes) {
    uint8_t *blob = NULL;
    size_t blob_bytes = 0;
    pvr_chunk_asset_view_t asset_view;
    pvr_chunk_asset_section_t texture_image_section;
    pvr_chunk_scene_asset_view_t scene_asset_view;
    pvr_chunk_scene_asset_workspace_requirements_t scene_requirements;
    pvr_chunk_model_table_view_t table_view;
    pvr_chunk_scene_hierarchy_view_t hierarchy_view;
    pvr_chunk_hierarchy_t hierarchy;
    pvr_chunk_texture_section_view_t texture_image_view;
    pvr_chunk_shape_section_view_t *shape_views = NULL;
    const pvr_chunk_shape_section_view_t **shape_view_pointers = NULL;
    pvr_chunk_model_view_t *model_views = NULL;
    pvr_chunk_hierarchy_node_t *hierarchy_nodes = NULL;
    void *workspace = NULL;
    const void *texture_image_data = NULL;
    size_t workspace_allocation = 0;
    size_t model;
    int result = -1;

    if(build_multi_asset_blob(
           streams, model_count, lz4_vertices, include_cooked_cache,
           scene, metadata, animations, animation_count,
           texture_section, texture_section_bytes,
           &blob, &blob_bytes) < 0 ||
       pvr_chunk_asset_open(blob, blob_bytes, &asset_view) < 0 ||
       pvr_chunk_scene_asset_open(
           &asset_view, &scene_asset_view) < 0 ||
       pvr_chunk_scene_asset_workspace_query(
           &scene_asset_view, &scene_requirements) < 0)
        goto out;
    if(scene_asset_view.model_count != model_count) {
        errno = EILSEQ;
        goto out;
    }
    table_view = scene_asset_view.model_table;
    hierarchy_view = scene_asset_view.hierarchy;
    if(texture_section &&
       (load_raw_section_type(
            &asset_view, PVR_CHUNK_ASSET_SECTION_TEXTURE_IMAGES,
            &texture_image_section, &texture_image_data) < 0 ||
        pvr_chunk_texture_section_open(
            texture_image_data, texture_image_section.decoded_bytes,
            &texture_image_view) < 0 ||
        texture_image_view.entry_count == 0)) {
        if(!errno)
            errno = EILSEQ;
        goto out;
    }
    model_views = calloc(model_count, sizeof(*model_views));
    hierarchy_nodes = calloc(scene_asset_view.node_count,
                             sizeof(*hierarchy_nodes));
    shape_views = calloc(model_count, sizeof(*shape_views));
    shape_view_pointers = calloc(model_count,
                                 sizeof(*shape_view_pointers));
    if(!model_views ||
       (scene_asset_view.node_count && !hierarchy_nodes) ||
       !shape_views || !shape_view_pointers) {
        errno = ENOMEM;
        goto out;
    }
    if(scene_requirements.bytes) {
        if(align_size_32(scene_requirements.bytes,
                         &workspace_allocation) < 0)
            goto out;
        workspace = aligned_alloc(scene_requirements.alignment,
                                  workspace_allocation);
        if(!workspace) {
            errno = ENOMEM;
            goto out;
        }
    }
    /* Verify the generated bytes through the same coherent, persistent
       scene-loading path used by target applications. */
    if(pvr_chunk_scene_asset_load(
           &scene_asset_view, pvr_chunk_asset_lz4_decode, NULL,
           workspace, workspace_allocation, model_views, model_count,
           hierarchy_nodes, scene_asset_view.node_count,
           &hierarchy) < 0)
        goto out;
    for(model = 0; model < model_count; ++model) {
        pvr_chunk_model_table_record_t record;

        if(pvr_chunk_model_table_record_get(
               &table_view, model, &record) < 0)
            goto out;
        if(validate_multi_model_metadata(
               &asset_view, &record, &metadata[model]) < 0)
            goto out;
        if(record.morph_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) {
            pvr_chunk_asset_section_t shape_section;
            const void *shape_data;

            if(record.morph_ordinal >= model_count ||
               load_raw_section_ordinal(
                   &asset_view, PVR_CHUNK_ASSET_SECTION_MORPH_TARGETS,
                   record.morph_ordinal, &shape_section,
                   &shape_data) < 0 ||
               pvr_chunk_shape_section_open(
                   shape_data, shape_section.decoded_bytes,
                   &shape_views[record.morph_ordinal]) < 0)
                goto out;
            shape_view_pointers[record.morph_ordinal] =
                &shape_views[record.morph_ordinal];
        }
        if(record.resource_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) {
            pvr_chunk_asset_section_t resource_section;
            pvr_chunk_resource_section_view_t resource_view;
            const void *resource_data;

            if(load_raw_section_ordinal(
                   &asset_view, PVR_CHUNK_ASSET_SECTION_RESOURCE_TABLE,
                   record.resource_ordinal, &resource_section,
                   &resource_data) < 0 ||
               pvr_chunk_resource_section_open(
                   resource_data, resource_section.decoded_bytes,
                   &resource_view) < 0 ||
               pvr_chunk_resource_section_validate_model(
                   &resource_view, &model_views[model]) < 0)
                goto out;
        }
        if(record.cooked_cache_ordinal != PVR_CHUNK_MODEL_SECTION_NONE) {
            pvr_chunk_asset_section_t cooked_section;
            pvr_chunk_cache_section_view_t cooked_view;
            pvr_chunk_cache_section_requirements_t cooked_requirements;
            pvr_chunk_model_cache_t cooked_cache;
            const void *cooked_data;
            void *cooked_storage;

            if(load_raw_section_ordinal(
                   &asset_view, PVR_CHUNK_ASSET_SECTION_COOKED_CACHE,
                   record.cooked_cache_ordinal, &cooked_section,
                   &cooked_data) < 0 ||
               pvr_chunk_cache_section_open(
                   cooked_data, cooked_section.decoded_bytes,
                   &cooked_view) < 0 ||
               pvr_chunk_cache_section_workspace_query(
                   &cooked_view, &cooked_requirements) < 0)
                goto out;
            cooked_storage = aligned_alloc(
                cooked_requirements.alignment,
                cooked_requirements.bytes);
            if(!cooked_storage) {
                errno = ENOMEM;
                goto out;
            }
            if(pvr_chunk_cache_section_materialize_ordinary(
                   &cooked_view, cooked_storage,
                   cooked_requirements.bytes, &cooked_cache) < 0 ||
               cooked_cache.vertex_count !=
                   model_views[model].info.index_references ||
               cooked_cache.strip_count !=
                   model_views[model].info.strips) {
                int cache_errno = errno ? errno : EILSEQ;

                free(cooked_storage);
                errno = cache_errno;
                goto out;
            }
            free(cooked_storage);
        }
    }
    if(animation_count) {
        pvr_chunk_asset_section_t catalog_section;
        pvr_chunk_animation_catalog_view_t catalog_view;
        const void *catalog_data;
        uint32_t transform_ordinal = 0;
        uint32_t morph_ordinal = 0;
        size_t animation;

        if(load_raw_section_type(
               &asset_view, PVR_CHUNK_ASSET_SECTION_ANIMATION_CATALOG,
               &catalog_section, &catalog_data) < 0 ||
           pvr_chunk_animation_catalog_open(
               catalog_data, catalog_section.decoded_bytes,
               &catalog_view) < 0 ||
           catalog_view.clip_count != animation_count ||
           pvr_chunk_animation_catalog_validate_asset(
               &catalog_view, &asset_view) < 0) {
            if(!errno)
                errno = EILSEQ;
            goto out;
        }
        for(animation = 0; animation < animation_count; ++animation) {
            const gltf_animation_metadata_t *expected =
                &animations[animation];
            pvr_chunk_animation_catalog_clip_t clip;
            size_t expected_name_bytes = expected->name ?
                strlen(expected->name) : 0;
            uint32_t expected_transform =
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
            uint32_t expected_morph =
                PVR_CHUNK_ANIMATION_CATALOG_SECTION_NONE;
            float expected_start = FLT_MAX;
            float expected_end = -FLT_MAX;

            if(expected->animation_track_count) {
                expected_transform = transform_ordinal++;
                expected_start = expected->animation.clip.start_time;
                expected_end = expected->animation.clip.end_time;
            }
            if(expected->morph_animation.binding_count) {
                expected_morph = morph_ordinal++;
                if(expected->morph_animation.start_time < expected_start)
                    expected_start = expected->morph_animation.start_time;
                if(expected->morph_animation.end_time > expected_end)
                    expected_end = expected->morph_animation.end_time;
            }

            if(pvr_chunk_animation_catalog_clip_get(
                   &catalog_view, animation, &clip) < 0 ||
               clip.name_bytes != expected_name_bytes ||
               (expected_name_bytes && memcmp(
                   clip.name, expected->name, expected_name_bytes)) ||
               clip.transform_ordinal != expected_transform ||
               clip.morph_ordinal != expected_morph ||
               clip.start_time != expected_start ||
               clip.end_time != expected_end) {
                if(!errno)
                    errno = EILSEQ;
                goto out;
            }
            if(expected->animation_track_count) {
                pvr_chunk_asset_section_t section;
                pvr_chunk_animation_section_view_t view;
                const void *data;

                if(load_raw_section_ordinal(
                       &asset_view, PVR_CHUNK_ASSET_SECTION_ANIMATION,
                       clip.transform_ordinal, &section, &data) < 0 ||
                   pvr_chunk_animation_section_open(
                       data, section.decoded_bytes, &view) < 0 ||
                   view.transform_count != scene->node_count) {
                    if(!errno)
                        errno = EILSEQ;
                    goto out;
                }
            }
            if(expected->morph_animation.binding_count &&
               validate_multi_morph_animation(
                   &asset_view, clip.morph_ordinal, &hierarchy_view,
                   &table_view, shape_view_pointers, model_count,
                   &expected->morph_animation) < 0)
                goto out;
        }
    }
    if(prepare_blob_output(target, blob, blob_bytes, temporary) < 0)
        goto out;
    *asset_bytes = blob_bytes;
    result = 0;

out:
    free(shape_view_pointers);
    free(shape_views);
    free(hierarchy_nodes);
    free(workspace);
    free(model_views);
    free(blob);
    return result;
}

static int prepare_output(const char *target, const void *words,
                          size_t word_count, size_t word_size,
                          temporary_output_t *temporary) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t target_length = strlen(target);
    FILE *file = NULL;
    int descriptor = -1;
    size_t word;
    int saved_errno;

    memset(temporary, 0, sizeof(*temporary));
    if(target_length > SIZE_MAX - sizeof(suffix)) {
        errno = EOVERFLOW;
        return -1;
    }
    temporary->path = malloc(target_length + sizeof(suffix));
    if(!temporary->path) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(temporary->path, target, target_length);
    memcpy(temporary->path + target_length, suffix, sizeof(suffix));

    /* The temporary lives beside the target, making the final rename atomic
       for each individual stream and avoiding partially written assets. */
    descriptor = mkstemp(temporary->path);
    if(descriptor < 0)
        goto fail;
    file = fdopen(descriptor, "wb");
    if(!file)
        goto fail;
    descriptor = -1;

    errno = 0;
    for(word = 0; word < word_count; ++word) {
        uint32_t value = word_size == sizeof(uint32_t) ?
            ((const uint32_t *)words)[word] :
            ((const uint16_t *)words)[word];

        if(write_word(file, value, word_size) < 0)
            goto fail;
    }
    if(fflush(file) < 0)
        goto fail;
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    else if(descriptor >= 0)
        close(descriptor);
    if(temporary->path)
        unlink(temporary->path);
    free(temporary->path);
    temporary->path = NULL;
    errno = saved_errno;
    return -1;
}

static int checked_fprintf(FILE *file, const char *format, ...) {
    va_list arguments;
    int rv;

    va_start(arguments, format);
    rv = vfprintf(file, format, arguments);
    va_end(arguments);
    if(rv < 0) {
        if(!errno)
            errno = EIO;
        return -1;
    }
    return 0;
}

static int write_c_array(FILE *file, const char *symbol, const char *suffix,
                         const void *words, size_t word_count,
                         size_t word_size) {
    const char *type = word_size == sizeof(uint32_t) ? "uint32_t" :
                                                       "uint16_t";
    const char *macro = word_size == sizeof(uint32_t) ? "UINT32_C" :
                                                        "UINT16_C";
    size_t per_line = word_size == sizeof(uint32_t) ? 4u : 8u;
    size_t word;

    if(checked_fprintf(file,
                       "alignas(%s) static const %s %s_%s[] = {\n",
                       type, type, symbol, suffix) < 0)
        return -1;
    for(word = 0; word < word_count; ++word) {
        uint32_t value = word_size == sizeof(uint32_t) ?
            ((const uint32_t *)words)[word] :
            ((const uint16_t *)words)[word];
        int line_end = word + 1u == word_count ||
                       (word + 1u) % per_line == 0;

        if(word % per_line == 0 && checked_fprintf(file, "    ") < 0)
            return -1;
        if(checked_fprintf(file, "%s(0x%0*x)%s", macro,
                           word_size == sizeof(uint32_t) ? 8 : 4,
                           (unsigned int)value,
                           word + 1u == word_count ? "" : ",") < 0)
            return -1;
        if(checked_fprintf(file, line_end ? "\n" : " ") < 0)
            return -1;
    }
    return checked_fprintf(file, "};\n\n");
}

static int write_c_float(FILE *file, float value) {
    uint32_t word = float_word(value);
    uint32_t exponent = (word >> 23) & UINT32_C(0xff);
    uint32_t fraction = word & UINT32_C(0x007fffff);
    const char *sign = word & UINT32_C(0x80000000) ? "-" : "";

    if(exponent == UINT32_C(0xff)) {
        errno = EDOM;
        return -1;
    }
    if(!exponent && !fraction)
        return checked_fprintf(file, "%s0x0p+0F", sign);
    if(!exponent)
        return checked_fprintf(file, "%s0x0.%06xp-126F", sign,
                               (unsigned int)(fraction << 1));
    return checked_fprintf(file, "%s0x1.%06xp%+dF", sign,
                           (unsigned int)(fraction << 1),
                           (int)exponent - 127);
}

static int write_c_model(FILE *file, const char *symbol,
                         const output_streams_t *streams) {
    if(checked_fprintf(
           file,
           "/* Generated by pvr-model-convert. */\n\n"
           "#include <dc/pvr_chunk_model.h>\n"
           "#include <stdalign.h>\n"
           "#include <stdint.h>\n\n"
           "_Static_assert(sizeof(uint32_t) == 4, "
           "\"compact vertex words require 32 bits\");\n"
           "_Static_assert(sizeof(uint16_t) == 2, "
           "\"compact polygon words require 16 bits\");\n\n") < 0 ||
       write_c_array(file, symbol, "vertex_words", streams->vertex_words,
                     streams->vertex_word_count, sizeof(uint32_t)) < 0 ||
       write_c_array(file, symbol, "polygon_words", streams->polygon_words,
                     streams->polygon_word_count, sizeof(uint16_t)) < 0 ||
       checked_fprintf(
           file,
           "const pvr_chunk_model_t %s = {\n"
           "    .vertex_words = %s_vertex_words,\n"
           "    .vertex_word_count = sizeof(%s_vertex_words) /\n"
           "                         sizeof(%s_vertex_words[0]),\n"
           "    .polygon_words = %s_polygon_words,\n"
           "    .polygon_word_count = sizeof(%s_polygon_words) /\n"
           "                          sizeof(%s_polygon_words[0]),\n"
           "    .center = { ",
           symbol, symbol, symbol, symbol, symbol, symbol, symbol) < 0 ||
       write_c_float(file, streams->center[0]) < 0 ||
       checked_fprintf(file, ", ") < 0 ||
       write_c_float(file, streams->center[1]) < 0 ||
       checked_fprintf(file, ", ") < 0 ||
       write_c_float(file, streams->center[2]) < 0 ||
       checked_fprintf(file, " },\n    .radius = ") < 0 ||
       write_c_float(file, streams->radius) < 0 ||
       checked_fprintf(file, "\n};\n") < 0)
        return -1;
    return 0;
}

static int prepare_c_output(const char *target, const char *symbol,
                            const output_streams_t *streams,
                            temporary_output_t *temporary) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t target_length = strlen(target);
    FILE *file = NULL;
    int descriptor = -1;
    int saved_errno;

    memset(temporary, 0, sizeof(*temporary));
    if(target_length > SIZE_MAX - sizeof(suffix)) {
        errno = EOVERFLOW;
        return -1;
    }
    temporary->path = malloc(target_length + sizeof(suffix));
    if(!temporary->path) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(temporary->path, target, target_length);
    memcpy(temporary->path + target_length, suffix, sizeof(suffix));

    descriptor = mkstemp(temporary->path);
    if(descriptor < 0)
        goto fail;
    file = fdopen(descriptor, "w");
    if(!file)
        goto fail;
    descriptor = -1;
    if(write_c_model(file, symbol, streams) < 0 || fflush(file) < 0)
        goto fail;
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    else if(descriptor >= 0)
        close(descriptor);
    if(temporary->path)
        unlink(temporary->path);
    free(temporary->path);
    temporary->path = NULL;
    errno = saved_errno;
    return -1;
}

static void discard_output(temporary_output_t *temporary) {
    if(temporary->path)
        unlink(temporary->path);
    free(temporary->path);
    temporary->path = NULL;
}

static int publish_output(temporary_output_t *temporary,
                          const char *target) {
    if(rename(temporary->path, target) < 0)
        return -1;
    free(temporary->path);
    temporary->path = NULL;
    return 0;
}

static void print_report(const source_model_t *source,
                         const output_streams_t *streams,
                         const pvr_chunk_model_info_t *info,
                         const material_table_t *materials,
                         const material_library_t *library) {
    printf("converted=1\n");
    printf("positions=%zu\n", source->position_count);
    printf("texcoords=%zu\n", source->texcoord_count);
    printf("normals=%zu\n", source->normal_count);
    printf("triangles=%zu\n", source->triangle_count);
    printf("strips_before=%zu\n", streams->source_strip_count);
    printf("strips_after=%zu\n", streams->output_strip_count);
    printf("triangles_joined=%zu\n",
           streams->source_strip_count - streams->output_strip_count);
    printf("strip_records=%zu\n", info->strip_records);
    printf("maximum_strip_vertices=%zu\n",
           info->maximum_strip_vertices);
    printf("texture_records=%zu\n", streams->texture_record_count);
    printf("material_records=%zu\n", streams->material_record_count);
    printf("material_bindings=%zu\n", materials->count);
    printf("material_libraries=%zu\n", library->file_count);
    printf("material_definitions=%zu\n", library->count);
    printf("vertex_words=%zu\n", streams->vertex_word_count);
    printf("polygon_words=%zu\n", streams->polygon_word_count);
    printf("center_x=%.9g\n", streams->center[0]);
    printf("center_y=%.9g\n", streams->center[1]);
    printf("center_z=%.9g\n", streams->center[2]);
    printf("radius=%.9g\n", streams->radius);
}

static void print_model_set_report(
    const source_model_t *sources, const output_streams_t *streams,
    const pvr_chunk_model_info_t *info, size_t model_count,
    const material_table_t *materials,
    const material_library_t *library) {
    size_t positions = 0;
    size_t texcoords = 0;
    size_t normals = 0;
    size_t triangles = 0;
    size_t strips_before = 0;
    size_t strips_after = 0;
    size_t strip_records = 0;
    size_t maximum_strip_vertices = 0;
    size_t texture_records = 0;
    size_t material_records = 0;
    size_t vertex_words = 0;
    size_t polygon_words = 0;
    float center[3];
    float radius;
    size_t model;

    if(model_count == 1u) {
        print_report(sources, streams, info, materials, library);
        return;
    }
    for(model = 0; model < model_count; ++model) {
        positions += sources[model].position_count;
        texcoords += sources[model].texcoord_count;
        normals += sources[model].normal_count;
        triangles += sources[model].triangle_count;
        strips_before += streams[model].source_strip_count;
        strips_after += streams[model].output_strip_count;
        strip_records += info[model].strip_records;
        if(info[model].maximum_strip_vertices > maximum_strip_vertices)
            maximum_strip_vertices = info[model].maximum_strip_vertices;
        texture_records += streams[model].texture_record_count;
        material_records += streams[model].material_record_count;
        vertex_words += streams[model].vertex_word_count;
        polygon_words += streams[model].polygon_word_count;
    }
    if(multi_asset_bounds(
           streams, model_count, center, &radius) < 0) {
        center[0] = center[1] = center[2] = 0.0f;
        radius = 0.0f;
    }
    printf("converted=%zu\n", model_count);
    printf("models=%zu\n", model_count);
    printf("positions=%zu\n", positions);
    printf("texcoords=%zu\n", texcoords);
    printf("normals=%zu\n", normals);
    printf("triangles=%zu\n", triangles);
    printf("strips_before=%zu\n", strips_before);
    printf("strips_after=%zu\n", strips_after);
    printf("triangles_joined=%zu\n", strips_before - strips_after);
    printf("strip_records=%zu\n", strip_records);
    printf("maximum_strip_vertices=%zu\n", maximum_strip_vertices);
    printf("texture_records=%zu\n", texture_records);
    printf("material_records=%zu\n", material_records);
    printf("material_bindings=%zu\n", materials->count);
    printf("material_libraries=%zu\n", library->file_count);
    printf("material_definitions=%zu\n", library->count);
    printf("vertex_words=%zu\n", vertex_words);
    printf("polygon_words=%zu\n", polygon_words);
    printf("center_x=%.9g\n", center[0]);
    printf("center_y=%.9g\n", center[1]);
    printf("center_z=%.9g\n", center[2]);
    printf("radius=%.9g\n", radius);
}

static int same_existing_file(const char *left, const char *right) {
    struct stat left_status;
    struct stat right_status;

    if(!strcmp(left, right))
        return 1;
    if(stat(left, &left_status) < 0) {
        if(errno == ENOENT)
            return 0;
        return -1;
    }
    if(stat(right, &right_status) < 0) {
        if(errno == ENOENT)
            return 0;
        return -1;
    }
    return left_status.st_dev == right_status.st_dev &&
           left_status.st_ino == right_status.st_ino;
}

static int output_target_admissible(const char *path) {
    struct stat status;

    if(stat(path, &status) < 0) {
        if(errno == ENOENT)
            return 0;
        return -1;
    }
    if(!S_ISREG(status.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    source_model_t source = { 0 };
    source_model_set_t gltf_sources = { 0 };
    source_model_t *source_models = &source;
    material_table_t materials = { 0 };
    material_library_t library = { 0 };
    output_streams_t streams = { 0 };
    output_streams_t *model_streams = &streams;
    pvr_scene_ir_t scene = { 0 };
    gltf_asset_metadata_t gltf_metadata = { 0 };
    pvr_chunk_skin_general_t rigid_skin_data = { 0 };
    pvr_chunk_skeleton_joint_t rigid_skeleton_joint = { 0 };
    pvr_chunk_skeleton_t rigid_skeleton_data = { 0 };
    pvr_chunk_shape_delta_t morph_delta = { 0 };
    pvr_chunk_shape_target_t morph_shape_target = { 0 };
    pvr_chunk_shape_set_t morph_shapes = { 0 };
    anim_vector_key_t animation_keys[2] = { 0 };
    anim_track_view_t animation_track = { 0 };
    anim_transform_tracks_t animation_transform = { 0 };
    anim_visibility_tracks_t animation_visibility = { 0 };
    anim_clip_view_t animation_clip = { 0 };
    pvr_chunk_model_info_t info;
    pvr_chunk_model_info_t *model_info = &info;
    temporary_output_t vertex_temporary = { 0 };
    temporary_output_t polygon_temporary = { 0 };
    const char *input;
    const char *vertex_output = NULL;
    const char *polygon_output = NULL;
    const char *c_output = NULL;
    const char *asset_output = NULL;
    const char *c_symbol = NULL;
    size_t asset_bytes = 0;
    size_t model_count = 1;
    size_t error_line = 0;
    pvr_chunk_skin_span_t *rigid_spans = NULL;
    pvr_chunk_skin_weight_t *rigid_weights = NULL;
    int flip_winding = 0;
    int flip_v = 0;
    int join_strips = 0;
    int emit_asset = 0;
    int lz4_vertices = 0;
    int section_directory = 0;
    int cooked_cache = 0;
    int scene_root = 0;
    int rigid_skin = 0;
    int morph_target = 0;
    float morph_offset[3] = { 0.0f, 0.0f, 0.0f };
    int animation_offset_set = 0;
    int gltf_input = 0;
    float animation_offset[3] = { 0.0f, 0.0f, 0.0f };
    int texture_identifier = -1;
    int argument = 1;
    int result = 2;

    while(argument < argc && argv[argument][0] == '-') {
        if(!strcmp(argv[argument], "--")) {
            ++argument;
            break;
        }
        if(!strcmp(argv[argument], "--help")) {
            usage(stdout, argv[0]);
            material_table_free(&materials);
            material_library_free(&library);
            return 0;
        }
        if(!strcmp(argv[argument], "--flip-winding"))
            flip_winding = 1;
        else if(!strcmp(argv[argument], "--flip-v"))
            flip_v = 1;
        else if(!strcmp(argv[argument], "--join-strips"))
            join_strips = 1;
        else if(!strcmp(argv[argument], "--texture-id")) {
            if(argument + 1 >= argc || texture_identifier >= 0 ||
               materials.count ||
               parse_texture_identifier(argv[argument + 1],
                                        &texture_identifier) < 0) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            argument += 2;
            continue;
        }
        else if(!strcmp(argv[argument], "--material")) {
            if(argument + 1 >= argc || texture_identifier >= 0 ||
               material_table_add(&materials, argv[argument + 1]) < 0) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            argument += 2;
            continue;
        }
        else if(!strcmp(argv[argument], "--material-library")) {
            size_t material_error_line = 0;

            if(argument + 1 >= argc ||
               load_material_library(argv[argument + 1], &library,
                                     &material_error_line) < 0) {
                int load_errno = errno ? errno : EINVAL;

                if(argument + 1 < argc && material_error_line)
                    fprintf(stderr, "%s:%zu: %s\n", argv[argument + 1],
                            material_error_line, strerror(load_errno));
                else if(argument + 1 < argc)
                    fprintf(stderr, "%s: %s\n", argv[argument + 1],
                            strerror(load_errno));
                else
                    usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return argument + 1 < argc &&
                       load_errno != ENOENT && load_errno != EACCES ? 1 : 2;
            }
            argument += 2;
            continue;
        }
        else if(!strcmp(argv[argument], "--emit-c")) {
            if(argument + 1 >= argc || c_symbol || emit_asset ||
               !valid_c_symbol(argv[argument + 1])) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            c_symbol = argv[argument + 1];
            argument += 2;
            continue;
        }
        else if(!strcmp(argv[argument], "--emit-asset")) {
            if(c_symbol || emit_asset) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            emit_asset = 1;
        }
        else if(!strcmp(argv[argument], "--lz4-vertices")) {
            if(lz4_vertices) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            lz4_vertices = 1;
        }
        else if(!strcmp(argv[argument], "--section-directory")) {
            if(section_directory) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            section_directory = 1;
        }
        else if(!strcmp(argv[argument], "--cooked-cache")) {
            if(cooked_cache) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            cooked_cache = 1;
        }
        else if(!strcmp(argv[argument], "--scene-root")) {
            if(scene_root) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            scene_root = 1;
        }
        else if(!strcmp(argv[argument], "--rigid-skin")) {
            if(rigid_skin) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            rigid_skin = 1;
        }
        else if(!strcmp(argv[argument], "--morph-target")) {
            if(argument + 3 >= argc || morph_target ||
               parse_float_token(argv[argument + 1], morph_offset + 0) < 0 ||
               parse_float_token(argv[argument + 2], morph_offset + 1) < 0 ||
               parse_float_token(argv[argument + 3], morph_offset + 2) < 0) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            morph_target = 1;
            argument += 4;
            continue;
        }
        else if(!strcmp(argv[argument], "--animation-offset")) {
            if(argument + 3 >= argc || animation_offset_set ||
               parse_float_token(argv[argument + 1],
                                 animation_offset + 0) < 0 ||
               parse_float_token(argv[argument + 2],
                                 animation_offset + 1) < 0 ||
               parse_float_token(argv[argument + 3],
                                 animation_offset + 2) < 0) {
                usage(stderr, argv[0]);
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            animation_offset_set = 1;
            argument += 4;
            continue;
        }
        else {
            usage(stderr, argv[0]);
            material_table_free(&materials);
            material_library_free(&library);
            return 2;
        }
        ++argument;
    }
    if(lz4_vertices && !emit_asset) {
        fprintf(stderr, "--lz4-vertices requires --emit-asset\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(section_directory && !emit_asset) {
        fprintf(stderr, "--section-directory requires --emit-asset\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(cooked_cache && !section_directory) {
        fprintf(stderr,
                "--cooked-cache requires --emit-asset "
                "--section-directory\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(scene_root && !section_directory) {
        fprintf(stderr,
                "--scene-root requires --emit-asset --section-directory\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(rigid_skin && !section_directory) {
        fprintf(stderr,
                "--rigid-skin requires --emit-asset --section-directory\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(morph_target && !section_directory) {
        fprintf(stderr,
                "--morph-target requires --emit-asset --section-directory\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(animation_offset_set && !scene_root) {
        fprintf(stderr,
                "--animation-offset requires --emit-asset "
                "--section-directory --scene-root\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(argc - argument != (c_symbol || emit_asset ? 2 : 3)) {
        usage(stderr, argv[0]);
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }

    input = argv[argument];
    gltf_input = source_is_gltf(input);
    if(gltf_input && (!emit_asset || !section_directory)) {
        fprintf(stderr,
                "glTF input requires --emit-asset --section-directory\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(gltf_input && (materials.count || library.count || scene_root ||
                      rigid_skin || morph_target || animation_offset_set)) {
        fprintf(stderr,
                "glTF input owns scene, material, skin, morph, and animation "
                "metadata\n");
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }
    if(c_symbol)
        c_output = argv[argument + 1];
    else if(emit_asset)
        asset_output = argv[argument + 1];
    else {
        vertex_output = argv[argument + 1];
        polygon_output = argv[argument + 2];
    }
    {
        const char *first_output = c_symbol ? c_output :
            (emit_asset ? asset_output : vertex_output);
        int input_first = same_existing_file(input, first_output);
        int input_polygon = c_symbol || emit_asset ? 0 :
            same_existing_file(input, polygon_output);
        int output_pair = c_symbol || emit_asset ? 0 :
            same_existing_file(vertex_output, polygon_output);
        size_t material_file;

        if(input_first < 0 || input_polygon < 0 || output_pair < 0) {
            fprintf(stderr, "path check failed: %s\n", strerror(errno));
            material_table_free(&materials);
            material_library_free(&library);
            return 2;
        }
        if(input_first || input_polygon || output_pair) {
            fprintf(stderr, "input and output paths must be distinct\n");
            material_table_free(&materials);
            material_library_free(&library);
            return 2;
        }
        for(material_file = 0; material_file < library.file_count;
            ++material_file) {
            int material_first = same_existing_file(
                library.paths[material_file], first_output);
            int material_polygon = c_symbol || emit_asset ? 0 :
                same_existing_file(library.paths[material_file],
                                   polygon_output);

            if(material_first < 0 || material_polygon < 0) {
                fprintf(stderr, "path check failed: %s\n", strerror(errno));
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
            if(material_first || material_polygon) {
                fprintf(stderr,
                        "material library and output paths must be distinct\n");
                material_table_free(&materials);
                material_library_free(&library);
                return 2;
            }
        }
    }
    if(output_target_admissible(c_symbol ? c_output :
                                (emit_asset ? asset_output :
                                              vertex_output)) < 0 ||
       (!c_symbol && !emit_asset &&
        output_target_admissible(polygon_output) < 0)) {
        fprintf(stderr, "output target is not a regular file: %s\n",
                strerror(errno));
        material_table_free(&materials);
        material_library_free(&library);
        return 2;
    }

    if((gltf_input ?
        load_gltf_source(input, &gltf_sources, flip_winding, flip_v,
                         texture_identifier, &library, &scene,
                         &gltf_metadata) :
        load_obj_source(input, &source, flip_winding, flip_v,
                        texture_identifier, &materials, &library,
                        &error_line)) < 0) {
        int load_errno = errno;

        if(error_line)
            fprintf(stderr, "%s:%zu: %s\n", input, error_line,
                    strerror(load_errno));
        else
            fprintf(stderr, "%s: %s\n", input, strerror(load_errno));
        result = !error_line &&
                 (load_errno == ENOENT || load_errno == EACCES) ? 2 : 1;
        goto out;
    }
    if(gltf_input) {
        source_models = gltf_sources.models;
        model_count = gltf_sources.count;
        if(model_count > 1u) {
            model_streams = calloc(model_count, sizeof(*model_streams));
            model_info = calloc(model_count, sizeof(*model_info));
            if(!model_streams || !model_info) {
                errno = ENOMEM;
                fprintf(stderr, "conversion failed: %s\n",
                        strerror(errno));
                goto out;
            }
        }
    }
    {
        size_t model;

        for(model = 0; model < model_count; ++model) {
            if(validate_texture_policy(&source_models[model]) < 0) {
                fprintf(stderr,
                        "textured faces require a resolved texture ID and "
                        "cannot be mixed with untextured faces\n");
                result = 1;
                goto out;
            }
            if(validate_material_policy(
                   &source_models[model], &library) < 0) {
                fprintf(stderr,
                        "every face requires one complete selected material "
                        "when a material library is supplied\n");
                result = 1;
                goto out;
            }
            if(generate_streams(
                   &source_models[model], &library, join_strips,
                   &model_streams[model]) < 0 ||
               validate_generated(
                   &source_models[model], &model_streams[model],
                   &model_info[model]) < 0) {
                fprintf(stderr, "conversion failed: %s\n",
                        strerror(errno));
                goto out;
            }
        }
    }
    if(scene_root && pvr_scene_ir_add_root_model(&scene, 0) < 0) {
        fprintf(stderr, "scene construction failed: %s\n", strerror(errno));
        goto out;
    }
    if(rigid_skin) {
        size_t vertex;

        if(!model_info[0].vertex_entries ||
           model_info[0].vertex_entries > UINT16_MAX + 1u ||
           model_info[0].vertex_entries > UINT32_MAX) {
            errno = EOVERFLOW;
            fprintf(stderr, "rigid skin construction failed: %s\n",
                    strerror(errno));
            goto out;
        }
        rigid_spans = calloc(model_info[0].vertex_entries,
                             sizeof(*rigid_spans));
        rigid_weights = calloc(model_info[0].vertex_entries,
                               sizeof(*rigid_weights));
        if(!rigid_spans || !rigid_weights) {
            errno = ENOMEM;
            fprintf(stderr, "rigid skin construction failed: %s\n",
                    strerror(errno));
            goto out;
        }
        for(vertex = 0; vertex < model_info[0].vertex_entries; ++vertex) {
            rigid_spans[vertex].vertex_index = (uint16_t)vertex;
            rigid_spans[vertex].weight_count = 1;
            rigid_spans[vertex].first_weight = (uint32_t)vertex;
            rigid_weights[vertex].joint = 0;
            rigid_weights[vertex].weight = UINT16_MAX;
        }
        rigid_skin_data.spans = rigid_spans;
        rigid_skin_data.span_count = model_info[0].vertex_entries;
        rigid_skin_data.weights = rigid_weights;
        rigid_skin_data.weight_count = model_info[0].vertex_entries;
        rigid_skin_data.joint_count = 1;
        if(scene_root) {
            rigid_skeleton_joint.node_index = 0;
            rigid_skeleton_joint.inverse_bind[0][0] = 1.0f;
            rigid_skeleton_joint.inverse_bind[1][1] = 1.0f;
            rigid_skeleton_joint.inverse_bind[2][2] = 1.0f;
            rigid_skeleton_joint.inverse_bind[3][3] = 1.0f;
            rigid_skeleton_data.joints = &rigid_skeleton_joint;
            rigid_skeleton_data.joint_count = 1;
            rigid_skeleton_data.node_count = scene.node_count;
        }
    }
    if(morph_target) {
        if(!model_info[0].vertex_entries) {
            errno = EILSEQ;
            fprintf(stderr, "morph construction failed: %s\n",
                    strerror(errno));
            goto out;
        }
        morph_delta.vertex_index = 0;
        morph_delta.delta.position.x = morph_offset[0];
        morph_delta.delta.position.y = morph_offset[1];
        morph_delta.delta.position.z = morph_offset[2];
        morph_shape_target.deltas = &morph_delta;
        morph_shape_target.delta_count = 1;
        morph_shapes.targets = &morph_shape_target;
        morph_shapes.target_count = 1;
    }
    if(animation_offset_set) {
        animation_keys[0].time = 0.0f;
        animation_keys[0].value.w = 1.0f;
        animation_keys[1].time = 1.0f;
        animation_keys[1].value.x = animation_offset[0];
        animation_keys[1].value.y = animation_offset[1];
        animation_keys[1].value.z = animation_offset[2];
        animation_keys[1].value.w = 1.0f;
        animation_track.track.kind = ANIM_VALUE_VECTOR;
        animation_track.track.interpolation = ANIM_INTERPOLATION_LINEAR;
        animation_track.track.keys = animation_keys;
        animation_track.track.key_count = 2;
        animation_track.track.stride = sizeof(animation_keys[0]);
        animation_track.start_time = 0.0f;
        animation_track.end_time = 1.0f;
        animation_transform.translation = &animation_track;
        animation_transform.fallback.translation.w = 1.0f;
        animation_transform.fallback.rotation.w = 1.0f;
        animation_transform.fallback.scale.x = 1.0f;
        animation_transform.fallback.scale.y = 1.0f;
        animation_transform.fallback.scale.z = 1.0f;
        animation_visibility.fallback = true;
        animation_clip.clip.transforms = &animation_transform;
        animation_clip.clip.transform_count = 1;
        animation_clip.clip.start_time = 0.0f;
        animation_clip.clip.end_time = 1.0f;
        animation_clip.clip.visibility = &animation_visibility;
    }
    if(c_symbol) {
        if(prepare_c_output(c_output, c_symbol, &model_streams[0],
                            &vertex_temporary) < 0) {
            fprintf(stderr, "output preparation failed: %s\n",
                    strerror(errno));
            goto out;
        }
        if(publish_output(&vertex_temporary, c_output) < 0) {
            fprintf(stderr, "%s: %s\n", c_output, strerror(errno));
            goto out;
        }
    }
    else if(emit_asset) {
        int prepare_result;

        if(gltf_input && (model_count > 1u ||
                          gltf_metadata.animation_count > 1u)) {
            prepare_result = prepare_multi_asset_output(
                asset_output, model_streams, model_count, lz4_vertices,
                cooked_cache, &scene,
                gltf_metadata.models,
                gltf_metadata.animations,
                gltf_metadata.animation_count,
                gltf_metadata.texture_section,
                gltf_metadata.texture_section_bytes,
                &vertex_temporary, &asset_bytes);
        }
        else {
            prepare_result = prepare_asset_output(
                asset_output, &model_streams[0], lz4_vertices,
                section_directory, cooked_cache,
                scene.node_count ? &scene : NULL,
                gltf_metadata.model_count &&
                    gltf_metadata.models[0].skin.span_count ?
                    &gltf_metadata.models[0].skin :
                    (rigid_skin ? &rigid_skin_data : NULL),
                gltf_metadata.model_count &&
                    gltf_metadata.models[0].skeleton.joint_count ?
                    &gltf_metadata.models[0].skeleton :
                    (rigid_skin && scene_root ?
                        &rigid_skeleton_data : NULL),
                gltf_metadata.model_count &&
                    gltf_metadata.models[0].shapes.target_count ?
                    &gltf_metadata.models[0].shapes :
                    (morph_target ? &morph_shapes : NULL),
                gltf_metadata.animation_count &&
                    gltf_metadata.animations[0].animation_track_count ?
                    &gltf_metadata.animations[0].animation :
                    (animation_offset_set ? &animation_clip : NULL),
                gltf_metadata.animation_count &&
                    gltf_metadata.animations[0].morph_animation.binding_count ?
                    &gltf_metadata.animations[0].morph_animation : NULL,
                gltf_metadata.texture_section,
                gltf_metadata.texture_section_bytes,
                &vertex_temporary, &asset_bytes);
        }
        if(prepare_result < 0) {
            fprintf(stderr, "asset preparation failed: %s\n",
                    strerror(errno));
            goto out;
        }
        if(publish_output(&vertex_temporary, asset_output) < 0) {
            fprintf(stderr, "%s: %s\n", asset_output, strerror(errno));
            goto out;
        }
    }
    else {
        if(prepare_output(vertex_output, model_streams[0].vertex_words,
                          model_streams[0].vertex_word_count,
                          sizeof(uint32_t),
                          &vertex_temporary) < 0 ||
           prepare_output(polygon_output, model_streams[0].polygon_words,
                          model_streams[0].polygon_word_count,
                          sizeof(uint16_t),
                          &polygon_temporary) < 0) {
            fprintf(stderr, "output preparation failed: %s\n",
                    strerror(errno));
            goto out;
        }
        if(publish_output(&vertex_temporary, vertex_output) < 0) {
            fprintf(stderr, "%s: %s\n", vertex_output, strerror(errno));
            goto out;
        }
        if(publish_output(&polygon_temporary, polygon_output) < 0) {
            fprintf(stderr, "%s: %s\n", polygon_output, strerror(errno));
            goto out;
        }
    }

    print_model_set_report(
        source_models, model_streams, model_info, model_count,
        &materials, &library);
    if(c_symbol)
        printf("c_symbol=%s\n", c_symbol);
    if(emit_asset) {
        printf("asset_bytes=%zu\n", asset_bytes);
        printf("vertex_codec=%s\n", lz4_vertices ? "lz4-frame" : "raw");
        if(section_directory)
            printf("asset_container=pcm2\n");
        if(scene.node_count)
            printf("hierarchy_nodes=%zu\n", scene.node_count);
        if(rigid_skin) {
            printf("general_skin_spans=%zu\n", rigid_skin_data.span_count);
            printf("general_skin_weights=%zu\n",
                   rigid_skin_data.weight_count);
            if(scene_root)
                printf("skeleton_joints=%zu\n",
                       rigid_skeleton_data.joint_count);
        }
        if(gltf_metadata.model_count) {
            size_t model;
            size_t span_count = 0;
            size_t weight_count = 0;
            size_t joint_count = 0;

            for(model = 0; model < gltf_metadata.model_count; ++model) {
                span_count += gltf_metadata.models[model].skin.span_count;
                weight_count +=
                    gltf_metadata.models[model].skin.weight_count;
                joint_count +=
                    gltf_metadata.models[model].skeleton.joint_count;
            }
            if(span_count) {
                printf("general_skin_spans=%zu\n", span_count);
                printf("general_skin_weights=%zu\n", weight_count);
                printf("skeleton_joints=%zu\n", joint_count);
            }
        }
        if(morph_target) {
            printf("morph_targets=%zu\n", morph_shapes.target_count);
            printf("morph_deltas=%zu\n",
                   morph_shape_target.delta_count);
        }
        if(gltf_metadata.model_count) {
            size_t model;
            size_t target_count = 0;
            size_t delta_count = 0;

            for(model = 0; model < gltf_metadata.model_count; ++model) {
                const pvr_chunk_shape_set_t *shapes =
                    &gltf_metadata.models[model].shapes;
                size_t target;

                target_count += shapes->target_count;
                for(target = 0; target < shapes->target_count; ++target)
                    delta_count += shapes->targets[target].delta_count;
            }
            if(target_count) {
                printf("morph_targets=%zu\n", target_count);
                printf("morph_deltas=%zu\n", delta_count);
            }
        }
        if(animation_offset_set) {
            printf("animation_transforms=%zu\n",
                   animation_clip.clip.transform_count);
            printf("animation_tracks=1\n");
            printf("animation_keys=%zu\n",
                   animation_track.track.key_count);
        }
        if(gltf_metadata.animation_count) {
            size_t animation;
            size_t transform_count = 0;
            size_t animation_track_count = 0;
            size_t animation_key_count = 0;
            size_t morph_binding_count = 0;
            size_t morph_track_count = 0;
            size_t morph_key_count = 0;

            for(animation = 0;
                animation < gltf_metadata.animation_count; ++animation) {
                const gltf_animation_metadata_t *clip =
                    &gltf_metadata.animations[animation];

                if(clip->animation_track_count)
                    transform_count += clip->animation.clip.transform_count;
                animation_track_count += clip->animation_track_count;
                animation_key_count += clip->animation_key_count;
                morph_binding_count +=
                    clip->morph_animation.binding_count;
                morph_track_count += clip->morph_animation_track_count;
                morph_key_count += clip->morph_animation_key_count;
            }
            printf("animation_clips=%zu\n",
                   gltf_metadata.animation_count);
            if(animation_track_count) {
                printf("animation_transforms=%zu\n", transform_count);
                printf("animation_tracks=%zu\n", animation_track_count);
                printf("animation_keys=%zu\n", animation_key_count);
            }
            if(morph_binding_count) {
                printf("morph_animation_bindings=%zu\n",
                       morph_binding_count);
                printf("morph_animation_tracks=%zu\n",
                       morph_track_count);
                printf("morph_animation_keys=%zu\n", morph_key_count);
            }
        }
    }
    result = 0;

out:
    free(rigid_weights);
    free(rigid_spans);
    discard_output(&vertex_temporary);
    discard_output(&polygon_temporary);
    if(model_streams == &streams)
        output_streams_free(&streams);
    else if(model_streams) {
        size_t model;

        for(model = 0; model < model_count; ++model)
            output_streams_free(&model_streams[model]);
        free(model_streams);
    }
    if(model_info != &info)
        free(model_info);
    pvr_scene_ir_free(&scene);
    gltf_asset_metadata_free(&gltf_metadata);
    if(gltf_input)
        source_model_set_free(&gltf_sources);
    else
        source_model_free(&source);
    material_table_free(&materials);
    material_library_free(&library);
    return result;
}
