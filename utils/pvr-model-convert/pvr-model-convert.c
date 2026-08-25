/* KallistiOS ##version##

   Host-side OBJ to compact PVR model converter.
   Copyright (C) 2026 Joseph Black

   The source boundary is intentionally narrower than OBJ as a whole. Vertex
   positions become indexed 32-bit records. Per-corner UVs and normals become
   16-bit strip attributes, so independently indexed OBJ attributes do not
   require vertex duplication. Every input face is already one triangle; this
   tool never guesses polygon triangulation or material-name policy.

   Emission is planned in memory, split before compact 16-bit fields overflow,
   and admitted by pvr_chunk_model_validate() before temporary files are
   created. Output words are serialized explicitly little-endian so host byte
   order cannot alter the asset.
*/

#define _POSIX_C_SOURCE 200809L

#include <dc/pvr_chunk_model.h>

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_POSITION_COUNT 65536u
#define MAX_VERTEX_BATCH ((UINT16_MAX - 1u) / 3u)
#define MAX_STRIP_COUNT UINT16_C(0x3fff)

typedef struct source_position {
    float value[3];
} source_position_t;

typedef struct source_texcoord {
    float value[2];
} source_texcoord_t;

typedef struct source_normal {
    float value[3];
} source_normal_t;

typedef struct source_corner {
    size_t position;
    size_t texcoord;
    size_t normal;
} source_corner_t;

typedef struct source_triangle {
    source_corner_t corner[3];
    uint8_t strip_type;
    int texture_identifier;
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
    source_triangle_t *triangles;
    size_t triangle_count;
    size_t triangle_capacity;
} source_model_t;

typedef struct source_strip {
    size_t first_triangle;
    size_t triangle_count;
    size_t vertex_count;
    uint8_t strip_type;
    int texture_identifier;
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
    size_t source_strip_count;
    size_t output_strip_count;
} output_streams_t;

typedef struct temporary_output {
    char *path;
} temporary_output_t;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [--flip-winding] [--flip-v] [--texture-id ID | "
            "--material NAME=ID ...] [--join-strips] [--] "
            "INPUT.obj VERTICES.bin POLYGONS.bin\n",
            program);
}

static void source_model_free(source_model_t *model) {
    free(model->positions);
    free(model->texcoords);
    free(model->normals);
    free(model->triangles);
    memset(model, 0, sizeof(*model));
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
        if(texcoord.value[component] < 0.0f ||
           texcoord.value[component] > 1.0f) {
            errno = ERANGE;
            return -1;
        }
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

static int triangle_type(source_triangle_t *triangle) {
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
    if(has_texcoord && has_normal)
        triangle->strip_type = PVR_CHUNK_STRIP_UV10_NORMAL;
    else if(has_texcoord)
        triangle->strip_type = PVR_CHUNK_STRIP_UV10;
    else if(has_normal)
        triangle->strip_type = PVR_CHUNK_STRIP_NORMAL;
    else
        triangle->strip_type = PVR_CHUNK_STRIP_INDEX;
    return 0;
}

static int append_triangle(source_model_t *model, char *cursor,
                           int flip_winding, int texture_identifier) {
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
    if(triangle_type(&triangle) < 0)
        return -1;
    if(texture_identifier < -1) {
        errno = ENOENT;
        return -1;
    }
    triangle.texture_identifier = texture_identifier;
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
                           int *texture_identifier) {
    char *name = next_token(&cursor);

    if(!materials->count) {
        errno = ENOTSUP;
        return -1;
    }
    if(!name || next_token(&cursor)) {
        errno = EILSEQ;
        return -1;
    }
    if(material_table_find(materials, name, texture_identifier) < 0) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int validate_references(const source_model_t *model) {
    size_t triangle;
    size_t corner;

    if(!model->position_count || !model->triangle_count) {
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

static int load_source(const char *path, source_model_t *model,
                       int flip_winding, int flip_v, int texture_identifier,
                       const material_table_t *materials,
                       size_t *error_line) {
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_length;
    size_t line_number = 0;
    int active_texture = materials->count ? -2 : texture_identifier;
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
                                 active_texture);
        else if(!strcmp(directive, "usemtl"))
            rv = select_material(cursor, materials, &active_texture);
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
        case PVR_CHUNK_STRIP_UV10:
            return 3u;
        case PVR_CHUNK_STRIP_NORMAL:
            return 4u;
        case PVR_CHUNK_STRIP_UV10_NORMAL:
            return 6u;
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
            .texture_identifier = initial->texture_identifier
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
               next->texture_identifier != strip.texture_identifier)
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
              plan->strips[first].texture_identifier) {
        size_t addition =
            1u + plan->strips[first + count].vertex_count * stride;

        if(addition > UINT16_MAX - payload_words)
            break;
        payload_words += addition;
        ++count;
    }
    return count;
}

static int calculate_sizes(const source_model_t *model,
                           const strip_plan_t *plan,
                           output_streams_t *streams) {
    size_t vertex_batches =
        (model->position_count + MAX_VERTEX_BATCH - 1u) / MAX_VERTEX_BATCH;
    size_t vertex_words;
    size_t polygon_words = 5u;
    size_t first = 0;
    int active_texture = -1;

    if(model->position_count > (SIZE_MAX - 1u - 2u * vertex_batches) / 3u) {
        errno = EOVERFLOW;
        return -1;
    }
    vertex_words = 1u + 2u * vertex_batches + 3u * model->position_count;

    while(first < plan->count) {
        size_t count = strip_batch(plan, first);
        size_t stride = strip_vertex_words(plan->strips[first].strip_type);
        size_t payload_words = 1u;
        size_t strip;
        size_t addition;
        int texture_identifier = plan->strips[first].texture_identifier;

        for(strip = 0; strip < count; ++strip)
            payload_words += 1u +
                plan->strips[first + strip].vertex_count * stride;
        addition = 2u + payload_words;

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

static uint16_t quantize_uv(float value) {
    return (uint16_t)lroundf(value * 1023.0f);
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
    if(type == PVR_CHUNK_STRIP_UV10 ||
       type == PVR_CHUNK_STRIP_UV10_NORMAL) {
        const source_texcoord_t *texcoord = &model->texcoords[corner->texcoord];

        *(*output)++ = quantize_uv(texcoord->value[0]);
        *(*output)++ = quantize_uv(texcoord->value[1]);
    }
    if(type == PVR_CHUNK_STRIP_NORMAL ||
       type == PVR_CHUNK_STRIP_UV10_NORMAL) {
        const source_normal_t *normal = &model->normals[corner->normal];
        size_t component;

        for(component = 0; component < 3u; ++component)
            *(*output)++ = quantize_normal(normal->value[component]);
    }
}

static int generate_streams(const source_model_t *model,
                            int join_strips, output_streams_t *streams) {
    strip_plan_t plan = { 0 };
    uint32_t *vertex_output;
    uint16_t *polygon_output;
    size_t first;
    int active_texture = -1;

    if(build_strip_plan(model, join_strips, &plan) < 0 ||
       calculate_sizes(model, &plan, streams) < 0 ||
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
        size_t payload_words;
        size_t position;

        if(count > MAX_VERTEX_BATCH)
            count = MAX_VERTEX_BATCH;
        payload_words = 1u + 3u * count;
        *vertex_output++ = PVR_CHUNK_VERTEX_XYZ |
                           ((uint32_t)payload_words << 16);
        *vertex_output++ = ((uint32_t)count << 16) | (uint32_t)first;
        for(position = 0; position < count; ++position) {
            size_t component;

            for(component = 0; component < 3u; ++component) {
                *vertex_output++ = float_word(
                    model->positions[first + position].value[component]);
            }
        }
        first += count;
    }
    *vertex_output++ = PVR_CHUNK_CONTROL_END;

    /* A white diffuse material establishes the common material state. Texture
       records below change only when the host binding resolves a new ID. */
    polygon_output = streams->polygon_words;
    *polygon_output++ = PVR_CHUNK_MATERIAL_DIFFUSE;
    *polygon_output++ = 2u;
    *polygon_output++ = UINT16_MAX;
    *polygon_output++ = UINT16_MAX;
    first = 0;
    while(first < plan.count) {
        size_t count = strip_batch(&plan, first);
        uint8_t type = plan.strips[first].strip_type;
        size_t stride = strip_vertex_words(type);
        size_t payload_words = 1u;
        size_t strip;
        int texture_identifier = plan.strips[first].texture_identifier;

        for(strip = 0; strip < count; ++strip)
            payload_words += 1u +
                plan.strips[first + strip].vertex_count * stride;

        if(texture_identifier >= 0 && texture_identifier != active_texture) {
            *polygon_output++ = PVR_CHUNK_TEXTURE;
            *polygon_output++ = (uint16_t)texture_identifier;
            active_texture = texture_identifier;
        }

        *polygon_output++ = type;
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
       info->strip_records != streams->strip_record_count) {
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
                         const material_table_t *materials) {
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
    printf("texture_records=%zu\n", streams->texture_record_count);
    printf("material_bindings=%zu\n", materials->count);
    printf("vertex_words=%zu\n", streams->vertex_word_count);
    printf("polygon_words=%zu\n", streams->polygon_word_count);
    printf("center_x=%.9g\n", streams->center[0]);
    printf("center_y=%.9g\n", streams->center[1]);
    printf("center_z=%.9g\n", streams->center[2]);
    printf("radius=%.9g\n", streams->radius);
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
    material_table_t materials = { 0 };
    output_streams_t streams = { 0 };
    pvr_chunk_model_info_t info;
    temporary_output_t vertex_temporary = { 0 };
    temporary_output_t polygon_temporary = { 0 };
    const char *input;
    const char *vertex_output;
    const char *polygon_output;
    size_t error_line = 0;
    int flip_winding = 0;
    int flip_v = 0;
    int join_strips = 0;
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
                return 2;
            }
            argument += 2;
            continue;
        }
        else {
            usage(stderr, argv[0]);
            material_table_free(&materials);
            return 2;
        }
        ++argument;
    }
    if(argc - argument != 3) {
        usage(stderr, argv[0]);
        material_table_free(&materials);
        return 2;
    }

    input = argv[argument];
    vertex_output = argv[argument + 1];
    polygon_output = argv[argument + 2];
    {
        int input_vertex = same_existing_file(input, vertex_output);
        int input_polygon = same_existing_file(input, polygon_output);
        int output_pair = same_existing_file(vertex_output, polygon_output);

        if(input_vertex < 0 || input_polygon < 0 || output_pair < 0) {
            fprintf(stderr, "path check failed: %s\n", strerror(errno));
            material_table_free(&materials);
            return 2;
        }
        if(input_vertex || input_polygon || output_pair) {
            fprintf(stderr, "input and output paths must be distinct\n");
            material_table_free(&materials);
            return 2;
        }
    }
    if(output_target_admissible(vertex_output) < 0 ||
       output_target_admissible(polygon_output) < 0) {
        fprintf(stderr, "output target is not a regular file: %s\n",
                strerror(errno));
        material_table_free(&materials);
        return 2;
    }

    if(load_source(input, &source, flip_winding, flip_v,
                   texture_identifier, &materials, &error_line) < 0) {
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
    if(validate_texture_policy(&source) < 0) {
        fprintf(stderr,
                "textured faces require a resolved texture ID and cannot "
                "be mixed with untextured faces\n");
        result = 1;
        goto out;
    }
    if(generate_streams(&source, join_strips, &streams) < 0 ||
       validate_generated(&source, &streams, &info) < 0) {
        fprintf(stderr, "conversion failed: %s\n", strerror(errno));
        goto out;
    }
    if(prepare_output(vertex_output, streams.vertex_words,
                      streams.vertex_word_count, sizeof(uint32_t),
                      &vertex_temporary) < 0 ||
       prepare_output(polygon_output, streams.polygon_words,
                      streams.polygon_word_count, sizeof(uint16_t),
                      &polygon_temporary) < 0) {
        fprintf(stderr, "output preparation failed: %s\n", strerror(errno));
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

    print_report(&source, &streams, &info, &materials);
    result = 0;

out:
    discard_output(&vertex_temporary);
    discard_output(&polygon_temporary);
    output_streams_free(&streams);
    source_model_free(&source);
    material_table_free(&materials);
    return result;
}
