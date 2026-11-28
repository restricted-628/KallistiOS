#define _FILE_OFFSET_BITS 64
#include <kosdec/acx.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { ENTRY_BYTES = 8, DEFAULT_ALIGNMENT = 4 };

struct item {
    uint32_t offset, bytes;
};

static int report(const char *path) {
    fprintf(stderr, "acx: %s: %s\n", path, strerror(errno ? errno : EIO));
    return 1;
}

static int report_archive(const char *path, enum kosdec_acx_status status) {
    static const char *const messages[] = {
        "ok", "end of member", "member not found", "invalid argument",
        "invalid member range", "truncated archive", "unsupported ACX header", "read error"
    };
    fprintf(stderr, "acx: %s: %s\n", path, messages[status]);
    return 1;
}

static ptrdiff_t file_read(void *context, uint64_t position, void *out, size_t bytes) {
    FILE *file = context;
    if(position > INT64_MAX || fseeko(file, (off_t)position, SEEK_SET))
        return -1;
    size_t got = fread(out, 1, bytes, file);
    return ferror(file) ? -1 : (ptrdiff_t)got;
}

static FILE *open_archive(const char *path, struct kosdec_acx *archive) {
    FILE *in = fopen(path, "rb");
    if(!in) {
        report(path);
        return NULL;
    }
    off_t length;
    if(fseeko(in, 0, SEEK_END) || (length = ftello(in)) < 0) {
        report(path);
        fclose(in);
        return NULL;
    }
    enum kosdec_acx_status status = kosdec_acx_open(archive, file_read, in, (uint64_t)length);
    if(status != KOSDEC_ACX_OK) {
        report_archive(path, status);
        fclose(in);
        return NULL;
    }
    return in;
}

static int list_archive(const struct kosdec_acx *archive, const char *path) {
    puts("id\toffset\tbytes");
    for(uint32_t id = 0; id < archive->count; ++id) {
        struct kosdec_acx_member member;
        enum kosdec_acx_status status = kosdec_acx_get(archive, id, &member);
        if(status != KOSDEC_ACX_OK)
            return report_archive(path, status);
        printf("%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\n", id, member.offset, member.bytes);
    }
    return fflush(stdout) ? report("stdout") : 0;
}

static bool copy_member(FILE *out, const struct kosdec_acx_member *member) {
    uint8_t buffer[16384];
    for(uint32_t offset = 0; offset < member->bytes; ) {
        size_t got;
        if(kosdec_acx_read(member, offset, buffer, sizeof(buffer), &got) != KOSDEC_ACX_OK) {
            errno = EIO;
            return false;
        }
        if(fwrite(buffer, 1, got, out) != got)
            return false;
        offset += (uint32_t)got;
    }
    return true;
}

static int finish_output(FILE *out, const char *path, bool ok) {
    int saved = ok ? 0 : (errno ? errno : EIO);
    if(fclose(out) && !saved)
        saved = errno ? errno : EIO;
    if(!saved)
        return 0;
    remove(path);
    errno = saved;
    return report(path);
}

static int extract_archive(const struct kosdec_acx *archive, const char *path,
    const char *directory) {
    size_t capacity = strlen(directory) + 32;
    char *output = malloc(capacity);
    if(!output)
        return report(directory);
    if(mkdir(directory, 0777)) {
        free(output);
        return report(directory);
    }
    uint32_t id = 0;
    int result = 0;
    for(; id < archive->count; ++id) {
        struct kosdec_acx_member member;
        enum kosdec_acx_status status = kosdec_acx_get(archive, id, &member);
        if(status != KOSDEC_ACX_OK) {
            result = report_archive(path, status);
            break;
        }
        snprintf(output, capacity, "%s/%010" PRIu32 ".bin", directory, id);
        FILE *out = fopen(output, "wbx");
        result = out ? finish_output(out, output, copy_member(out, &member)) : report(output);
        if(result)
            break;
    }
    if(result) {
        while(id) {
            snprintf(output, capacity, "%s/%010" PRIu32 ".bin", directory, --id);
            remove(output);
        }
        remove(directory);
    }
    free(output);
    return result;
}

static bool write_pair(FILE *out, uint32_t first, uint32_t second) {
    uint8_t row[ENTRY_BYTES];
    for(unsigned i = 0; i < 4; ++i) {
        row[i] = (uint8_t)(first >> (24u - 8u * i));
        row[i + 4] = (uint8_t)(second >> (24u - 8u * i));
    }
    return fwrite(row, 1, sizeof(row), out) == sizeof(row);
}

static uint64_t align_up(uint64_t n, uint32_t alignment) {
    return (n + alignment - 1u) / alignment * alignment;
}

static bool padding(FILE *out, uint64_t bytes) {
    static const uint8_t zeros[4096];
    while(bytes) {
        size_t chunk = bytes < sizeof(zeros) ? (size_t)bytes : sizeof(zeros);
        if(fwrite(zeros, 1, chunk, out) != chunk)
            return false;
        bytes -= chunk;
    }
    return true;
}

static bool copy_input(FILE *out, const char *path, uint32_t bytes) {
    FILE *in = fopen(path, "rb");
    if(!in)
        return false;
    uint8_t buffer[16384];
    bool ok = true;
    while(bytes) {
        size_t chunk = bytes < sizeof(buffer) ? bytes : sizeof(buffer);
        if(fread(buffer, 1, chunk, in) != chunk || fwrite(buffer, 1, chunk, out) != chunk) {
            ok = false;
            break;
        }
        bytes -= (uint32_t)chunk;
    }
    if(ok && (fgetc(in) != EOF || ferror(in))) {
        errno = EIO;
        ok = false;
    }
    if(fclose(in))
        ok = false;
    return ok;
}

static int pack(const char *output, char *const *paths, uint32_t count, uint32_t alignment) {
    uint64_t header_bytes = ENTRY_BYTES * (1u + (uint64_t)count);
    uint64_t first = align_up(header_bytes, alignment);
    if(first > UINT32_MAX) {
        errno = EFBIG;
        return report(output);
    }
    struct item *items = calloc(count, sizeof(*items));
    if(!items)
        return report(output);
    uint64_t position = first;
    int result = 0;
    for(uint32_t id = 0; id < count; ++id) {
        struct stat info;
        if(stat(paths[id], &info)) {
            result = report(paths[id]);
            break;
        }
        if(!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX) {
            errno = EINVAL;
            result = report(paths[id]);
            break;
        }
        items[id] = (struct item){(uint32_t)position, (uint32_t)info.st_size};
        position += align_up(items[id].bytes, alignment);
        if(position > UINT32_MAX) {
            errno = EFBIG;
            result = report(output);
            break;
        }
    }

    if(!result) {
        FILE *out = fopen(output, "wbx");
        if(!out)
            result = report(output);
        else {
            bool ok = write_pair(out, 0, count);
            for(uint32_t id = 0; ok && id < count; ++id)
                ok = write_pair(out, items[id].offset, items[id].bytes);
            ok = ok && padding(out, first - header_bytes);
            for(uint32_t id = 0; ok && id < count; ++id) {
                uint32_t bytes = items[id].bytes;
                ok = copy_input(out, paths[id], bytes) && padding(out, align_up(bytes, alignment) - bytes);
            }
            result = finish_output(out, output, ok);
        }
    }
    free(items);
    return result;
}

static char *default_output(const char *list) {
    const char *basename = strrchr(list, '/');
    const char *dot = strrchr(basename ? basename + 1 : list, '.');
    size_t stem = dot ? (size_t)(dot - list) : strlen(list);
    char *out = malloc(stem + sizeof(".acx"));
    if(out) {
        memcpy(out, list, stem);
        memcpy(out + stem, ".acx", sizeof(".acx"));
    }
    return out;
}

static int pack_list(const char *list, const char *output, uint32_t alignment) {
    FILE *in = fopen(list, "rb");
    if(!in)
        return report(list);
    char **paths = NULL, *line = NULL;
    size_t count = 0, capacity = 0, line_size = 0;
    int result = 0;
    ssize_t bytes;
    while((bytes = getline(&line, &line_size, in)) >= 0) {
        while(bytes && (line[bytes - 1] == '\r' || line[bytes - 1] == '\n'))
            line[--bytes] = '\0';
        if(!bytes)
            continue;
        if(memchr(line, '\0', (size_t)bytes) || count == UINT32_MAX / ENTRY_BYTES - 1u) {
            errno = EINVAL;
            result = report(list);
            break;
        }
        if(count == capacity) {
            size_t grown = capacity ? 2 * capacity : 32;
            if(grown > SIZE_MAX / sizeof(*paths)) {
                errno = EFBIG;
                result = report(list);
                break;
            }
            char **next = realloc(paths, grown * sizeof(*paths));
            if(!next) {
                result = report(list);
                break;
            }
            paths = next;
            capacity = grown;
        }
        for(char *p = line; *p; ++p)
            if(*p == '\\')
                *p = '/';
        paths[count] = strdup(line);
        if(!paths[count]) {
            result = report(list);
            break;
        }
        ++count;
    }
    if(!result && (!feof(in) || !count)) {
        if(feof(in))
            errno = EINVAL;
        result = report(list);
    }
    if(fclose(in) && !result)
        result = report(list);
    char *derived = output ? NULL : default_output(list);
    if(!result) {
        const char *path = output ? output : derived;
        result = path ? pack(path, paths, (uint32_t)count, alignment) : report(list);
    }
    free(derived);
    free(line);
    for(size_t i = 0; i < count; ++i)
        free(paths[i]);
    free(paths);
    return result;
}

int main(int argc, char **argv) {
    if(argc >= 4 && !strcmp(argv[1], "pack"))
        return pack(argv[2], argv + 3, (uint32_t)(argc - 3), DEFAULT_ALIGNMENT);
    if(argc >= 3 && !strcmp(argv[1], "pack-list")) {
        const char *output = NULL;
        uint32_t alignment = DEFAULT_ALIGNMENT;
        for(int i = 3; i < argc; ++i) {
            if(!strncmp(argv[i], "-al", 3)) {
                char *end;
                errno = 0;
                unsigned long value = strtoul(argv[i] + 3, &end, 10);
                if(argv[i][3] < '0' || argv[i][3] > '9' ||
                   errno || *end || !value || value > UINT32_MAX)
                    goto usage;
                alignment = (uint32_t)value;
            } else if(!output && argv[i][0] != '-')
                output = argv[i];
            else
                goto usage;
        }
        return pack_list(argv[2], output, alignment);
    }

    bool list = argc == 3 && !strcmp(argv[1], "list");
    bool extract = argc == 4 && !strcmp(argv[1], "extract");
    if(list || extract) {
        struct kosdec_acx archive;
        FILE *in = open_archive(argv[2], &archive);
        if(!in)
            return 1;
        int result = list ? list_archive(&archive, argv[2]) :
                            extract_archive(&archive, argv[2], argv[3]);
        fclose(in);
        return result;
    }

usage:
    fputs("usage: acx list ARCHIVE\n"
          "       acx extract ARCHIVE NEW_DIRECTORY\n"
          "       acx pack OUTPUT INPUT...\n"
          "       acx pack-list LIST.fls [OUTPUT.acx] [-alBYTES]\n", stderr);
    return 2;
}
