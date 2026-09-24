/* KallistiOS ##version##

   Host-side compact PVR model inspector.
   Copyright (C) 2026 Joseph Black
*/

#include <dc/pvr_chunk_model.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURE_IDENTIFIER_COUNT 8192u
#define TEXTURE_IDENTIFIER_BYTES (TEXTURE_IDENTIFIER_COUNT / 8u)

typedef struct model_stats {
    size_t texture_references;
    size_t distinct_textures;
    size_t maximum_strip_vertices;
} model_stats_t;

static void usage(FILE *stream, const char *program) {
    fprintf(stream,
            "usage: %s [--center X Y Z] [--radius R] [--] "
            "VERTICES POLYGONS\n",
            program);
}

static int parse_float(const char *text, float *result) {
    char *end;
    float value;

    errno = 0;
    value = strtof(text, &end);
    if(errno == ERANGE || end == text || *end != '\0' || !isfinite(value)) {
        errno = EINVAL;
        return -1;
    }

    *result = value;
    return 0;
}

static uint16_t decode_u16le(const unsigned char *bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t decode_u32le(const unsigned char *bytes) {
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int read_file(const char *path, size_t word_size, void **words,
                     size_t *word_count) {
    unsigned char *bytes = NULL;
    void *decoded = NULL;
    FILE *file = NULL;
    long length;
    size_t byte_count;
    size_t count;
    size_t i;
    int saved_errno;

    *words = NULL;
    *word_count = 0;

    file = fopen(path, "rb");
    if(!file)
        return -1;
    if(fseek(file, 0, SEEK_END) < 0)
        goto fail;
    length = ftell(file);
    if(length < 0)
        goto fail;
    if(fseek(file, 0, SEEK_SET) < 0)
        goto fail;

    if((uintmax_t)length > SIZE_MAX) {
        errno = EOVERFLOW;
        goto fail;
    }
    byte_count = (size_t)length;
    if(!byte_count || byte_count % word_size) {
        errno = EINVAL;
        goto fail;
    }
    count = byte_count / word_size;
    if(count > SIZE_MAX / word_size) {
        errno = EOVERFLOW;
        goto fail;
    }

    bytes = malloc(byte_count);
    decoded = malloc(byte_count);
    if(!bytes || !decoded) {
        errno = ENOMEM;
        goto fail;
    }
    errno = 0;
    if(fread(bytes, 1, byte_count, file) != byte_count) {
        if(!errno)
            errno = EIO;
        goto fail;
    }
    if(fclose(file) < 0) {
        file = NULL;
        goto fail;
    }
    file = NULL;

    if(word_size == sizeof(uint32_t)) {
        uint32_t *output = decoded;

        for(i = 0; i < count; ++i)
            output[i] = decode_u32le(bytes + i * sizeof(uint32_t));
    }
    else {
        uint16_t *output = decoded;

        for(i = 0; i < count; ++i)
            output[i] = decode_u16le(bytes + i * sizeof(uint16_t));
    }

    free(bytes);
    *words = decoded;
    *word_count = count;
    return 0;

fail:
    saved_errno = errno ? errno : EIO;
    if(file)
        fclose(file);
    free(bytes);
    free(decoded);
    errno = saved_errno;
    return -1;
}

static int collect_stats(const pvr_chunk_model_t *model,
                         model_stats_t *stats) {
    unsigned char texture_ids[TEXTURE_IDENTIFIER_BYTES] = { 0 };
    pvr_chunk_iterator_t iterator;
    pvr_chunk_record_t record;
    int rv;

    memset(stats, 0, sizeof(*stats));
    if(pvr_chunk_polygon_iterator_init(&iterator, model->polygon_words,
                                       model->polygon_word_count) < 0)
        return -1;

    while((rv = pvr_chunk_iterator_next(&iterator, &record)) > 0) {
        if(record.record_class == PVR_CHUNK_RECORD_TEXTURE) {
            const uint16_t encoded = *(const uint16_t *)record.payload;
            const unsigned int identifier = encoded & UINT16_C(0x1fff);
            const unsigned int byte = identifier >> 3;
            const unsigned int bit = identifier & 7u;

            ++stats->texture_references;
            if(!(texture_ids[byte] & (1u << bit))) {
                texture_ids[byte] |= (unsigned char)(1u << bit);
                ++stats->distinct_textures;
            }
        }
        else if(record.record_class == PVR_CHUNK_RECORD_STRIP) {
            pvr_chunk_strip_iterator_t strip_iterator;
            pvr_chunk_strip_view_t strip;
            int strip_rv;

            if(pvr_chunk_strip_iterator_init(&strip_iterator, &record) < 0)
                return -1;
            while((strip_rv = pvr_chunk_strip_iterator_next(&strip_iterator,
                                                            &strip)) > 0) {
                if(strip.vertex_count > stats->maximum_strip_vertices)
                    stats->maximum_strip_vertices = strip.vertex_count;
            }
            if(strip_rv < 0)
                return -1;
        }
    }

    return rv < 0 ? -1 : 0;
}

static void print_report(const pvr_chunk_model_t *model,
                         const pvr_chunk_model_info_t *info,
                         const model_stats_t *stats) {
    printf("valid=1\n");
    printf("vertex_words=%zu\n", model->vertex_word_count);
    printf("polygon_words=%zu\n", model->polygon_word_count);
    printf("vertex_records=%zu\n", info->vertex_records);
    printf("vertex_entries=%zu\n", info->vertex_entries);
    printf("shape_records=%zu\n", info->shape_records);
    printf("polygon_records=%zu\n", info->polygon_records);
    printf("material_records=%zu\n", info->material_records);
    printf("strip_records=%zu\n", info->strip_records);
    printf("strips=%zu\n", info->strips);
    printf("triangles=%zu\n", info->triangles);
    printf("index_references=%zu\n", info->index_references);
    printf("polygon_cache_records=%zu\n", info->polygon_cache_records);
    printf("polygon_draw_records=%zu\n", info->polygon_draw_records);
    printf("requirements=0x%08" PRIx32 "\n", info->requirements);
    printf("maximum_vertex_index=%" PRIu32 "\n",
           info->maximum_vertex_index);
    printf("texture_references=%zu\n", stats->texture_references);
    printf("distinct_textures=%zu\n", stats->distinct_textures);
    printf("maximum_strip_vertices=%zu\n",
           info->maximum_strip_vertices);
}

int main(int argc, char **argv) {
    pvr_chunk_model_t model = {
        .center = { 0.0f, 0.0f, 0.0f },
        .radius = 0.0f
    };
    pvr_chunk_model_info_t info;
    model_stats_t stats;
    void *vertex_words = NULL;
    void *polygon_words = NULL;
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
        if(!strcmp(argv[argument], "--center")) {
            if(argument + 3 >= argc ||
               parse_float(argv[argument + 1], &model.center[0]) < 0 ||
               parse_float(argv[argument + 2], &model.center[1]) < 0 ||
               parse_float(argv[argument + 3], &model.center[2]) < 0) {
                usage(stderr, argv[0]);
                return 2;
            }
            argument += 4;
            continue;
        }
        if(!strcmp(argv[argument], "--radius")) {
            if(argument + 1 >= argc ||
               parse_float(argv[argument + 1], &model.radius) < 0 ||
               model.radius < 0.0f) {
                usage(stderr, argv[0]);
                return 2;
            }
            argument += 2;
            continue;
        }

        usage(stderr, argv[0]);
        return 2;
    }

    if(argc - argument != 2) {
        usage(stderr, argv[0]);
        return 2;
    }
    if(read_file(argv[argument], sizeof(uint32_t), &vertex_words,
                 &model.vertex_word_count) < 0) {
        fprintf(stderr, "%s: %s\n", argv[argument], strerror(errno));
        goto out;
    }
    model.vertex_words = vertex_words;
    if(read_file(argv[argument + 1], sizeof(uint16_t), &polygon_words,
                 &model.polygon_word_count) < 0) {
        fprintf(stderr, "%s: %s\n", argv[argument + 1], strerror(errno));
        goto out;
    }
    model.polygon_words = polygon_words;

    if(pvr_chunk_model_validate(&model, &info) < 0) {
        fprintf(stderr, "invalid model: %s\n", strerror(errno));
        result = 1;
        goto out;
    }
    if(collect_stats(&model, &stats) < 0) {
        fprintf(stderr, "inspection failed: %s\n", strerror(errno));
        result = 1;
        goto out;
    }
    if(stats.maximum_strip_vertices != info.maximum_strip_vertices) {
        fprintf(stderr, "inspection failed: inconsistent strip summary\n");
        result = 1;
        goto out;
    }

    print_report(&model, &info, &stats);
    result = 0;

out:
    free(vertex_words);
    free(polygon_words);
    return result;
}
