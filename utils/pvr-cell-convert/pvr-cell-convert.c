#define _POSIX_C_SOURCE 200809L

/* KallistiOS ##version##

   Host-side declarative cell-sprite compiler.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_cell_asset.h>
#include <dc/pvr_sprite_geometry.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stb_image.h"

typedef struct region_decl {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    float pivot_x;
    float pivot_y;
} region_decl_t;

typedef struct stream_decl {
    pvr_cell_key_t *keys;
    size_t key_count;
    size_t key_capacity;
    float time_offset;
    float time_max;
    uint32_t repeat;
} stream_decl_t;

typedef struct manifest {
    region_decl_t *regions;
    size_t region_count;
    size_t region_capacity;
    pvr_cell_state_t *cells;
    size_t cell_count;
    size_t cell_capacity;
    stream_decl_t *streams;
    size_t stream_count;
    size_t stream_capacity;
} manifest_t;

typedef struct output_image {
    uint8_t *bytes;
    size_t size;
} output_image_t;

static const char *program_name = "pvr-cell-convert";

/* The shared geometry units retain their target submission entry points even
   though this host tool only uses their validation helpers. */
int pvr_prim(const void *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    (void)list;
    (void)data;
    (void)size;
    return 0;
}

static void usage(FILE *output) {
    fprintf(output,
            "usage: %s --image IMAGE --symbol SYMBOL "
            "INPUT.pcell OUTPUT.pca OUTPUT.c\n",
            program_name);
}

static void manifest_free(manifest_t *manifest) {
    size_t index;

    if(!manifest)
        return;
    for(index = 0; index < manifest->stream_count; ++index)
        free(manifest->streams[index].keys);
    free(manifest->streams);
    free(manifest->cells);
    free(manifest->regions);
    memset(manifest, 0, sizeof(*manifest));
}

static int grow_array(void **data, size_t *capacity, size_t count,
                      size_t element_size) {
    size_t next;
    void *grown;

    if(count < *capacity)
        return 0;
    next = *capacity ?
        (*capacity <= SIZE_MAX / 2u ? *capacity * 2u : SIZE_MAX) : 8u;
    if(next <= count)
        next = count == SIZE_MAX ? 0u : count + 1u;
    if(!next) {
        errno = EOVERFLOW;
        return -1;
    }
    if(next > SIZE_MAX / element_size) {
        errno = EOVERFLOW;
        return -1;
    }
    grown = realloc(*data, next * element_size);
    if(!grown)
        return -1;
    *data = grown;
    *capacity = next;
    return 0;
}

static size_t tokenize(char *line, char **tokens, size_t capacity) {
    size_t count = 0;
    char *cursor = line;

    while(*cursor) {
        while(*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
              *cursor == '\n')
            ++cursor;
        if(!*cursor || *cursor == '#')
            break;
        if(count == capacity)
            return SIZE_MAX;
        tokens[count++] = cursor;
        while(*cursor && *cursor != ' ' && *cursor != '\t' &&
              *cursor != '\r' && *cursor != '\n' && *cursor != '#')
            ++cursor;
        if(*cursor == '#') {
            *cursor = '\0';
            break;
        }
        if(*cursor)
            *cursor++ = '\0';
    }
    return count;
}

static int parse_u32(const char *text, int base, uint32_t *value) {
    char *end;
    unsigned long parsed;

    if(*text == '-')
        return -1;
    errno = 0;
    parsed = strtoul(text, &end, base);
    if(errno || !*text || *end || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int parse_size(const char *text, size_t *value) {
    uint32_t parsed;

    if(parse_u32(text, 10, &parsed) < 0)
        return -1;
    *value = parsed;
    return 0;
}

static int parse_i32(const char *text, int32_t *value) {
    char *end;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if(errno || !*text || *end || parsed < INT32_MIN || parsed > INT32_MAX)
        return -1;
    *value = (int32_t)parsed;
    return 0;
}

static int parse_float(const char *text, float *value) {
    char *end;
    float parsed;

    errno = 0;
    parsed = strtof(text, &end);
    if(errno || !*text || *end || !isfinite(parsed))
        return -1;
    *value = parsed;
    return 0;
}

static int append_region(manifest_t *manifest, char **tokens) {
    region_decl_t region;

    if(parse_u32(tokens[1], 10, &region.x) < 0 ||
       parse_u32(tokens[2], 10, &region.y) < 0 ||
       parse_u32(tokens[3], 10, &region.width) < 0 ||
       parse_u32(tokens[4], 10, &region.height) < 0 ||
       parse_float(tokens[5], &region.pivot_x) < 0 ||
       parse_float(tokens[6], &region.pivot_y) < 0 ||
       !region.width || !region.height) {
        errno = EINVAL;
        return -1;
    }
    if(grow_array((void **)&manifest->regions,
                  &manifest->region_capacity, manifest->region_count,
                  sizeof(*manifest->regions)) < 0)
        return -1;
    manifest->regions[manifest->region_count++] = region;
    return 0;
}

static int append_cell(manifest_t *manifest, char **tokens) {
    pvr_cell_state_t cell;
    size_t index;

    memset(&cell, 0, sizeof(cell));
    if(parse_size(tokens[1], &cell.atlas_cell_index) < 0 ||
       parse_float(tokens[2], &cell.offset.x) < 0 ||
       parse_float(tokens[3], &cell.offset.y) < 0 ||
       parse_float(tokens[4], &cell.offset.z) < 0 ||
       parse_float(tokens[5], &cell.rotation) < 0 ||
       parse_float(tokens[6], &cell.scale_x) < 0 ||
       parse_float(tokens[7], &cell.scale_y) < 0 ||
       parse_i32(tokens[8], &cell.priority) < 0 ||
       parse_u32(tokens[9], 0, &cell.flags) < 0 ||
       parse_u32(tokens[10], 0, &cell.material_id) < 0)
        goto invalid;
    for(index = 0; index < 4u; ++index) {
        if(parse_u32(tokens[11u + index], 0, &cell.argb[index]) < 0 ||
           parse_u32(tokens[15u + index], 0, &cell.oargb[index]) < 0)
            goto invalid;
    }
    cell.offset.w = 1.0f;
    if(cell.atlas_cell_index >= manifest->region_count ||
       cell.scale_x <= 0.0f || cell.scale_y <= 0.0f ||
       (cell.flags & ~(PVR_CELL_FLIP_U | PVR_CELL_FLIP_V |
                       PVR_CELL_HIDDEN)))
        goto invalid;
    if(grow_array((void **)&manifest->cells, &manifest->cell_capacity,
                  manifest->cell_count, sizeof(*manifest->cells)) < 0)
        return -1;
    manifest->cells[manifest->cell_count++] = cell;
    return 0;

invalid:
    errno = EINVAL;
    return -1;
}

static int append_stream(manifest_t *manifest, char **tokens) {
    stream_decl_t stream;

    memset(&stream, 0, sizeof(stream));
    if(parse_float(tokens[1], &stream.time_offset) < 0 ||
       parse_float(tokens[2], &stream.time_max) < 0 ||
       stream.time_max <= 0.0f)
        goto invalid;
    if(!strcmp(tokens[3], "repeat"))
        stream.repeat = 1;
    else if(strcmp(tokens[3], "clamp"))
        goto invalid;
    if(grow_array((void **)&manifest->streams,
                  &manifest->stream_capacity, manifest->stream_count,
                  sizeof(*manifest->streams)) < 0)
        return -1;
    manifest->streams[manifest->stream_count++] = stream;
    return 0;

invalid:
    errno = EINVAL;
    return -1;
}

static int append_key(stream_decl_t *stream, pvr_cell_key_t *key) {
    if(grow_array((void **)&stream->keys, &stream->key_capacity,
                  stream->key_count, sizeof(*stream->keys)) < 0)
        return -1;
    stream->keys[stream->key_count++] = *key;
    return 0;
}

static int parse_key_common(char **tokens, pvr_cell_key_t *key) {
    memset(key, 0, sizeof(*key));
    if(parse_float(tokens[1], &key->time) < 0 || key->time < 0.0f ||
       parse_size(tokens[2], &key->slot_index) < 0) {
        errno = EINVAL;
        return -1;
    }
    key->value.offset.w = 1.0f;
    return 0;
}

static int parse_key(char **tokens, size_t count, stream_decl_t *stream) {
    pvr_cell_key_t key;
    size_t index;

    if(count < 3u || parse_key_common(tokens, &key) < 0)
        return -1;
    if(!strcmp(tokens[0], "key-atlas") && count == 4u) {
        key.fields = PVR_CELL_KEY_ATLAS_CELL;
        if(parse_size(tokens[3], &key.value.atlas_cell_index) < 0)
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-offset") && count == 6u) {
        key.fields = PVR_CELL_KEY_OFFSET;
        if(parse_float(tokens[3], &key.value.offset.x) < 0 ||
           parse_float(tokens[4], &key.value.offset.y) < 0 ||
           parse_float(tokens[5], &key.value.offset.z) < 0)
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-rotation") && count == 4u) {
        key.fields = PVR_CELL_KEY_ROTATION;
        if(parse_float(tokens[3], &key.value.rotation) < 0)
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-scale") && count == 5u) {
        key.fields = PVR_CELL_KEY_SCALE;
        if(parse_float(tokens[3], &key.value.scale_x) < 0 ||
           parse_float(tokens[4], &key.value.scale_y) < 0 ||
           key.value.scale_x <= 0.0f || key.value.scale_y <= 0.0f)
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-priority") && count == 4u) {
        key.fields = PVR_CELL_KEY_PRIORITY;
        if(parse_i32(tokens[3], &key.value.priority) < 0)
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-flags") && count == 4u) {
        key.fields = PVR_CELL_KEY_FLAGS;
        if(parse_u32(tokens[3], 0, &key.value.flags) < 0 ||
           (key.value.flags & ~(PVR_CELL_FLIP_U | PVR_CELL_FLIP_V |
                                PVR_CELL_HIDDEN)))
            goto invalid;
    }
    else if(!strcmp(tokens[0], "key-material") && count == 4u) {
        key.fields = PVR_CELL_KEY_MATERIAL;
        if(parse_u32(tokens[3], 0, &key.value.material_id) < 0)
            goto invalid;
    }
    else if((!strcmp(tokens[0], "key-diffuse") ||
             !strcmp(tokens[0], "key-specular")) && count == 7u) {
        uint32_t *colors;

        if(tokens[0][4] == '-' && tokens[0][5] == 'd') {
            key.fields = PVR_CELL_KEY_DIFFUSE;
            colors = key.value.argb;
        }
        else {
            key.fields = PVR_CELL_KEY_SPECULAR;
            colors = key.value.oargb;
        }
        for(index = 0; index < 4u; ++index) {
            if(parse_u32(tokens[3u + index], 0, &colors[index]) < 0)
                goto invalid;
        }
    }
    else
        goto invalid;
    return append_key(stream, &key);

invalid:
    errno = EINVAL;
    return -1;
}

static int parse_manifest(const char *path, manifest_t *manifest) {
    FILE *input = fopen(path, "r");
    char line[2048];
    size_t line_number = 0;
    int saw_header = 0;
    int in_stream = 0;
    int failed = 0;

    if(!input)
        return -1;
    while(fgets(line, sizeof(line), input)) {
        char *tokens[32];
        size_t count;

        ++line_number;
        if(!strchr(line, '\n') && !feof(input)) {
            errno = EOVERFLOW;
            failed = 1;
            break;
        }
        count = tokenize(line, tokens, 32u);
        if(count == SIZE_MAX) {
            errno = E2BIG;
            failed = 1;
            break;
        }
        if(!count)
            continue;
        if(!saw_header) {
            if(count != 2u || strcmp(tokens[0], "pvr-cell") ||
               strcmp(tokens[1], "1")) {
                errno = EINVAL;
                failed = 1;
                break;
            }
            saw_header = 1;
            continue;
        }
        if(!strcmp(tokens[0], "region") && count == 7u && !in_stream)
            failed = append_region(manifest, tokens) < 0;
        else if(!strcmp(tokens[0], "cell") && count == 19u && !in_stream)
            failed = append_cell(manifest, tokens) < 0;
        else if(!strcmp(tokens[0], "stream") && count == 4u &&
                !in_stream) {
            failed = append_stream(manifest, tokens) < 0;
            if(!failed)
                in_stream = 1;
        }
        else if(!strcmp(tokens[0], "end-stream") && count == 1u &&
                in_stream)
            in_stream = 0;
        else if(in_stream)
            failed = parse_key(tokens, count,
                               &manifest->streams[
                                   manifest->stream_count - 1u]) < 0;
        else {
            errno = EINVAL;
            failed = 1;
        }
        if(failed)
            break;
    }
    if(ferror(input) && !failed) {
        failed = 1;
        errno = EIO;
    }
    if(fclose(input) && !failed)
        failed = 1;
    if(!failed && (!saw_header || in_stream || !manifest->region_count ||
                   !manifest->cell_count)) {
        errno = EINVAL;
        failed = 1;
    }
    if(failed) {
        int saved = errno ? errno : EINVAL;

        fprintf(stderr, "%s:%zu: invalid cell manifest: %s\n",
                path, line_number, strerror(saved));
        errno = saved;
        return -1;
    }
    return 0;
}

static int validate_manifest(manifest_t *manifest, uint32_t image_width,
                             uint32_t image_height) {
    pvr_cell_stream_t *streams = NULL;
    size_t required;
    size_t index;
    int result = -1;

    for(index = 0; index < manifest->region_count; ++index) {
        region_decl_t *region = &manifest->regions[index];

        if(region->x > image_width || region->width > image_width - region->x ||
           region->y > image_height ||
           region->height > image_height - region->y ||
           region->pivot_x < 0.0f || region->pivot_x > region->width ||
           region->pivot_y < 0.0f || region->pivot_y > region->height) {
            errno = ERANGE;
            return -1;
        }
    }
    for(index = 0; index < manifest->cell_count; ++index) {
        if(manifest->cells[index].atlas_cell_index >=
           manifest->region_count) {
            errno = ERANGE;
            return -1;
        }
    }
    if(manifest->stream_count) {
        streams = calloc(manifest->stream_count, sizeof(*streams));
        if(!streams)
            return -1;
    }
    for(index = 0; index < manifest->stream_count; ++index) {
        size_t key;

        streams[index].keys = manifest->streams[index].keys;
        streams[index].key_count = manifest->streams[index].key_count;
        streams[index].time_offset = manifest->streams[index].time_offset;
        streams[index].time_max = manifest->streams[index].time_max;
        streams[index].repeat = manifest->streams[index].repeat;
        for(key = 0; key < streams[index].key_count; ++key) {
            pvr_cell_key_t *item = &manifest->streams[index].keys[key];

            if(item->slot_index >= manifest->cell_count ||
               ((item->fields & PVR_CELL_KEY_ATLAS_CELL) &&
                item->value.atlas_cell_index >= manifest->region_count)) {
                errno = ERANGE;
                goto done;
            }
        }
    }
    if(pvr_cell_asset_measure(manifest->cells, manifest->cell_count,
                              streams, manifest->stream_count,
                              &required) < 0)
        goto done;
    result = 0;

done:
    free(streams);
    return result;
}

static int build_asset(const manifest_t *manifest, output_image_t *output) {
    pvr_cell_stream_t *streams = NULL;
    pvr_cell_asset_view_t view;
    size_t required;
    size_t index;
    int result = -1;

    if(manifest->stream_count) {
        streams = calloc(manifest->stream_count, sizeof(*streams));
        if(!streams)
            return -1;
    }
    for(index = 0; index < manifest->stream_count; ++index) {
        streams[index].keys = manifest->streams[index].keys;
        streams[index].key_count = manifest->streams[index].key_count;
        streams[index].time_offset = manifest->streams[index].time_offset;
        streams[index].time_max = manifest->streams[index].time_max;
        streams[index].repeat = manifest->streams[index].repeat;
    }
    if(pvr_cell_asset_measure(manifest->cells, manifest->cell_count,
                              streams, manifest->stream_count,
                              &required) < 0)
        goto done;
    output->bytes = malloc(required);
    if(!output->bytes)
        goto done;
    output->size = required;
    if(pvr_cell_asset_encode(manifest->cells, manifest->cell_count,
                             streams, manifest->stream_count,
                             output->bytes, output->size, NULL) < 0 ||
       pvr_cell_asset_open(output->bytes, output->size, &view) < 0)
        goto done;
    result = 0;

done:
    if(result < 0) {
        free(output->bytes);
        memset(output, 0, sizeof(*output));
    }
    free(streams);
    return result;
}

static int symbol_valid(const char *symbol) {
    static const char *const keywords[] = {
        "alignas", "alignof", "asm", "auto", "bool", "break", "case",
        "char", "const", "constexpr", "continue", "default", "do",
        "double", "else", "enum", "extern", "false", "float", "for",
        "goto", "if", "inline", "int", "long", "nullptr", "register",
        "restrict", "return", "short", "signed", "sizeof", "static",
        "static_assert", "struct", "switch", "thread_local", "true",
        "typedef", "typeof", "typeof_unqual", "union", "unsigned",
        "void", "volatile", "while"
    };
    size_t index;

    if(!symbol || !((*symbol >= 'A' && *symbol <= 'Z') ||
                    (*symbol >= 'a' && *symbol <= 'z')))
        return 0;
    for(index = 1; symbol[index]; ++index) {
        char byte = symbol[index];

        if(!((byte >= 'A' && byte <= 'Z') ||
             (byte >= 'a' && byte <= 'z') ||
             (byte >= '0' && byte <= '9') || byte == '_') || index >= 57u)
            return 0;
    }
    for(index = 0; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if(!strcmp(symbol, keywords[index]))
            return 0;
    }
    return 1;
}

static int write_atlas(FILE *output, const manifest_t *manifest,
                       uint32_t image_width, uint32_t image_height,
                       const char *symbol) {
    size_t index;

    if(fprintf(output,
               "/* Generated by pvr-cell-convert. */\n"
               "#include <dc/pvr_sprite_geometry.h>\n\n"
               "static const pvr_sprite_cell_t %s_cells[] = {\n",
               symbol) < 0)
        return -1;
    for(index = 0; index < manifest->region_count; ++index) {
        const region_decl_t *region = &manifest->regions[index];
        double u0 = (double)region->x / image_width;
        double v0 = (double)region->y / image_height;
        double u1 = (double)(region->x + region->width) / image_width;
        double v1 = (double)(region->y + region->height) / image_height;
        double origin_x = (double)region->pivot_x / region->width;
        double origin_y = (double)region->pivot_y / region->height;

        if(fprintf(output,
                   "    { %aF, %aF, %aF, %aF, %aF, %aF, %aF, %aF },\n",
                   (double)region->width, (double)region->height,
                   origin_x, origin_y, u0, v0, u1, v1) < 0)
            return -1;
    }
    if(fprintf(output,
               "};\n\n"
               "const pvr_sprite_atlas_t %s = {\n"
               "    %s_cells,\n"
               "    sizeof(%s_cells) / sizeof(%s_cells[0])\n"
               "};\n",
               symbol, symbol, symbol, symbol) < 0)
        return -1;
    return ferror(output) ? -1 : 0;
}

static char *temporary_path(const char *path) {
    size_t length = strlen(path);
    char *temporary;

    if(length > SIZE_MAX - 12u) {
        errno = EOVERFLOW;
        return NULL;
    }
    temporary = malloc(length + 12u);
    if(!temporary)
        return NULL;
    memcpy(temporary, path, length);
    memcpy(temporary + length, ".tmp.XXXXXX", 12u);
    return temporary;
}

static int publish_outputs(const char *asset_path, const char *atlas_path,
                           const output_image_t *asset,
                           const manifest_t *manifest,
                           uint32_t image_width, uint32_t image_height,
                           const char *symbol) {
    char *asset_tmp = temporary_path(asset_path);
    char *atlas_tmp = temporary_path(atlas_path);
    FILE *output = NULL;
    int descriptor = -1;
    int result = -1;

    if(!asset_tmp || !atlas_tmp)
        goto done;
    descriptor = mkstemp(asset_tmp);
    if(descriptor < 0)
        goto done;
    output = fdopen(descriptor, "wb");
    if(!output)
        goto done;
    descriptor = -1;
    if(fwrite(asset->bytes, 1, asset->size, output) != asset->size) {
        fclose(output);
        output = NULL;
        goto done;
    }
    if(fclose(output)) {
        output = NULL;
        goto done;
    }
    output = NULL;
    descriptor = mkstemp(atlas_tmp);
    if(descriptor < 0)
        goto done;
    output = fdopen(descriptor, "w");
    if(!output)
        goto done;
    descriptor = -1;
    if(write_atlas(output, manifest, image_width, image_height, symbol) < 0) {
        fclose(output);
        output = NULL;
        goto done;
    }
    if(fclose(output)) {
        output = NULL;
        goto done;
    }
    output = NULL;
    if(rename(asset_tmp, asset_path) || rename(atlas_tmp, atlas_path))
        goto done;
    result = 0;

done:
    if(output)
        fclose(output);
    if(descriptor >= 0)
        close(descriptor);
    if(result < 0) {
        if(asset_tmp)
            remove(asset_tmp);
        if(atlas_tmp)
            remove(atlas_tmp);
    }
    free(atlas_tmp);
    free(asset_tmp);
    return result;
}

int main(int argc, char **argv) {
    const char *image_path = NULL;
    const char *symbol = NULL;
    const char *manifest_path = NULL;
    const char *asset_path = NULL;
    const char *atlas_path = NULL;
    manifest_t manifest;
    output_image_t asset;
    int image_width;
    int image_height;
    int image_components;
    int index;
    int result = EXIT_FAILURE;

    memset(&manifest, 0, sizeof(manifest));
    memset(&asset, 0, sizeof(asset));
    if(argc > 0 && argv[0] && *argv[0])
        program_name = argv[0];
    for(index = 1; index < argc; ++index) {
        if(!strcmp(argv[index], "--image") && index + 1 < argc &&
           !image_path)
            image_path = argv[++index];
        else if(!strcmp(argv[index], "--symbol") && index + 1 < argc &&
                !symbol)
            symbol = argv[++index];
        else if(argv[index][0] == '-') {
            usage(stderr);
            goto done;
        }
        else if(!manifest_path)
            manifest_path = argv[index];
        else if(!asset_path)
            asset_path = argv[index];
        else if(!atlas_path)
            atlas_path = argv[index];
        else {
            usage(stderr);
            goto done;
        }
    }
    if(!image_path || !symbol_valid(symbol) || !manifest_path ||
       !asset_path || !atlas_path || !strcmp(asset_path, atlas_path) ||
       !strcmp(asset_path, manifest_path) ||
       !strcmp(atlas_path, manifest_path) ||
       !strcmp(asset_path, image_path) || !strcmp(atlas_path, image_path)) {
        usage(stderr);
        goto done;
    }
    if(!stbi_info(image_path, &image_width, &image_height,
                  &image_components) || image_width <= 0 || image_height <= 0) {
        fprintf(stderr, "%s: cannot inspect image: %s\n",
                program_name, stbi_failure_reason());
        goto done;
    }
    if(parse_manifest(manifest_path, &manifest) < 0 ||
       validate_manifest(&manifest, (uint32_t)image_width,
                         (uint32_t)image_height) < 0 ||
       build_asset(&manifest, &asset) < 0) {
        if(errno)
            fprintf(stderr, "%s: %s\n", program_name, strerror(errno));
        goto done;
    }
    if(publish_outputs(asset_path, atlas_path, &asset, &manifest,
                       (uint32_t)image_width, (uint32_t)image_height,
                       symbol) < 0) {
        fprintf(stderr, "%s: cannot publish output: %s\n",
                program_name, strerror(errno));
        goto done;
    }
    printf("regions=%zu\ncells=%zu\nstreams=%zu\nasset_bytes=%zu\n",
           manifest.region_count, manifest.cell_count,
           manifest.stream_count, asset.size);
    result = EXIT_SUCCESS;

done:
    free(asset.bytes);
    manifest_free(&manifest);
    return result;
}
