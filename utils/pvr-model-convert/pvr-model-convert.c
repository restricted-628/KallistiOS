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
} source_triangle_t;

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

typedef struct output_streams {
    uint32_t *vertex_words;
    size_t vertex_word_count;
    uint16_t *polygon_words;
    size_t polygon_word_count;
    float center[3];
    float radius;
    size_t strip_record_count;
    int texture_identifier;
} output_streams_t;

typedef struct temporary_output {
    char *path;
} temporary_output_t;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [--flip-winding] [--flip-v] [--texture-id ID] [--] "
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
                           int flip_winding) {
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

static int validate_texture_policy(const source_model_t *model,
                                   int texture_identifier) {
    int saw_textured = 0;
    int saw_untextured = 0;
    size_t triangle;

    for(triangle = 0; triangle < model->triangle_count; ++triangle) {
        if(model->triangles[triangle].corner[0].texcoord == SIZE_MAX)
            saw_untextured = 1;
        else
            saw_textured = 1;
    }
    if((saw_textured && texture_identifier < 0) ||
       (texture_identifier >= 0 && saw_untextured)) {
        errno = EILSEQ;
        return -1;
    }
    return 0;
}

static int load_source(const char *path, source_model_t *model,
                       int flip_winding, int flip_v, size_t *error_line) {
    FILE *file = fopen(path, "r");
    char *line = NULL;
    size_t line_capacity = 0;
    size_t line_length;
    size_t line_number = 0;
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
            rv = append_triangle(model, cursor, flip_winding);
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

static size_t triangle_batch(const source_model_t *model, size_t first) {
    size_t stride = strip_vertex_words(model->triangles[first].strip_type);
    size_t per_triangle = 1u + 3u * stride;
    size_t maximum = (UINT16_MAX - 1u) / per_triangle;
    size_t count = 1u;

    /* Both the payload word count and declared strip count are 16-bit
       fields. */
    if(maximum > MAX_STRIP_COUNT)
        maximum = MAX_STRIP_COUNT;
    while(count < maximum && first + count < model->triangle_count &&
          model->triangles[first + count].strip_type ==
              model->triangles[first].strip_type)
        ++count;
    return count;
}

static int calculate_sizes(const source_model_t *model,
                           output_streams_t *streams) {
    size_t vertex_batches =
        (model->position_count + MAX_VERTEX_BATCH - 1u) / MAX_VERTEX_BATCH;
    size_t vertex_words;
    size_t polygon_words = streams->texture_identifier >= 0 ? 7u : 5u;
    size_t first = 0;

    if(model->position_count > (SIZE_MAX - 1u - 2u * vertex_batches) / 3u) {
        errno = EOVERFLOW;
        return -1;
    }
    vertex_words = 1u + 2u * vertex_batches + 3u * model->position_count;

    while(first < model->triangle_count) {
        size_t count = triangle_batch(model, first);
        size_t stride = strip_vertex_words(model->triangles[first].strip_type);
        size_t per_triangle = 1u + 3u * stride;
        size_t addition = 3u + count * per_triangle;

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
                            output_streams_t *streams,
                            int texture_identifier) {
    uint32_t *vertex_output;
    uint16_t *polygon_output;
    size_t first;

    streams->texture_identifier = texture_identifier;
    if(calculate_sizes(model, streams) < 0 ||
       calculate_bounds(model, streams) < 0)
        return -1;
    streams->vertex_words =
        malloc(streams->vertex_word_count * sizeof(*streams->vertex_words));
    streams->polygon_words =
        malloc(streams->polygon_word_count * sizeof(*streams->polygon_words));
    if(!streams->vertex_words || !streams->polygon_words) {
        errno = ENOMEM;
        return -1;
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

    /* One explicit texture ID and a white diffuse material establish the
       complete persistent state used by every following triangle strip. */
    polygon_output = streams->polygon_words;
    if(streams->texture_identifier >= 0) {
        *polygon_output++ = PVR_CHUNK_TEXTURE;
        *polygon_output++ = (uint16_t)streams->texture_identifier;
    }
    *polygon_output++ = PVR_CHUNK_MATERIAL_DIFFUSE;
    *polygon_output++ = 2u;
    *polygon_output++ = UINT16_MAX;
    *polygon_output++ = UINT16_MAX;
    first = 0;
    while(first < model->triangle_count) {
        size_t count = triangle_batch(model, first);
        uint8_t type = model->triangles[first].strip_type;
        size_t stride = strip_vertex_words(type);
        size_t payload_words = 1u + count * (1u + 3u * stride);
        size_t triangle;

        *polygon_output++ = type;
        *polygon_output++ = (uint16_t)payload_words;
        *polygon_output++ = (uint16_t)count;
        for(triangle = 0; triangle < count; ++triangle) {
            const source_triangle_t *source =
                &model->triangles[first + triangle];
            size_t corner;

            *polygon_output++ = 3u;
            for(corner = 0; corner < 3u; ++corner)
                emit_corner(&polygon_output, model, &source->corner[corner],
                            type);
        }
        first += count;
    }
    *polygon_output++ = PVR_CHUNK_CONTROL_END;

    if((size_t)(vertex_output - streams->vertex_words) !=
           streams->vertex_word_count ||
       (size_t)(polygon_output - streams->polygon_words) !=
           streams->polygon_word_count) {
        errno = EPROTO;
        return -1;
    }
    return 0;
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
       info->index_references != source->triangle_count * 3u ||
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
                         const pvr_chunk_model_info_t *info) {
    printf("converted=1\n");
    printf("positions=%zu\n", source->position_count);
    printf("texcoords=%zu\n", source->texcoord_count);
    printf("normals=%zu\n", source->normal_count);
    printf("triangles=%zu\n", source->triangle_count);
    printf("strip_records=%zu\n", info->strip_records);
    if(streams->texture_identifier >= 0)
        printf("texture_identifier=%d\n", streams->texture_identifier);
    else
        printf("texture_identifier=none\n");
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
            return 0;
        }
        if(!strcmp(argv[argument], "--flip-winding"))
            flip_winding = 1;
        else if(!strcmp(argv[argument], "--flip-v"))
            flip_v = 1;
        else if(!strcmp(argv[argument], "--texture-id")) {
            if(argument + 1 >= argc || texture_identifier >= 0 ||
               parse_texture_identifier(argv[argument + 1],
                                        &texture_identifier) < 0) {
                usage(stderr, argv[0]);
                return 2;
            }
            argument += 2;
            continue;
        }
        else {
            usage(stderr, argv[0]);
            return 2;
        }
        ++argument;
    }
    if(argc - argument != 3) {
        usage(stderr, argv[0]);
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
            return 2;
        }
        if(input_vertex || input_polygon || output_pair) {
            fprintf(stderr, "input and output paths must be distinct\n");
            return 2;
        }
    }
    if(output_target_admissible(vertex_output) < 0 ||
       output_target_admissible(polygon_output) < 0) {
        fprintf(stderr, "output target is not a regular file: %s\n",
                strerror(errno));
        return 2;
    }

    if(load_source(input, &source, flip_winding, flip_v, &error_line) < 0) {
        int load_errno = errno;

        if(error_line)
            fprintf(stderr, "%s:%zu: %s\n", input, error_line,
                    strerror(load_errno));
        else
            fprintf(stderr, "%s: %s\n", input, strerror(load_errno));
        result = load_errno == ENOENT || load_errno == EACCES ? 2 : 1;
        goto out;
    }
    if(validate_texture_policy(&source, texture_identifier) < 0) {
        fprintf(stderr,
                "textured faces require one --texture-id and all faces "
                "must use UVs\n");
        result = 1;
        goto out;
    }
    if(generate_streams(&source, &streams, texture_identifier) < 0 ||
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

    print_report(&source, &streams, &info);
    result = 0;

out:
    discard_output(&vertex_temporary);
    discard_output(&polygon_temporary);
    output_streams_free(&streams);
    source_model_free(&source);
    return result;
}
