#define _FILE_OFFSET_BITS 64
#include <kosdec/afs.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

enum { PAIR_BYTES = 8, AFSLNK_HEADER = 512 * 1024, RECORD_BYTES = KOSDEC_AFS_NAME_BYTES + KOSDEC_AFS_METADATA_BYTES };
struct item {
    struct kosdec_afs_member source;
    const char *path;
    uint32_t offset;
};

static ptrdiff_t file_read_at(void *context, uint64_t position, void *out, size_t bytes) {
    FILE *file = context;
    if(position > INT64_MAX || fseeko(file, (off_t)position, SEEK_SET))
        return -1;
    size_t n = fread(out, 1, bytes, file);
    return ferror(file) ? -1 : (ptrdiff_t)n;
}

static int report_status(const char *path, enum kosdec_afs_status status) {
    static const char *const messages[] = {
        "ok", "end of member", "member not found", "invalid argument",
        "invalid archive", "truncated archive", "unsupported archive", "read error",
        "partition loading", "waiting for input", "insufficient workspace", "partition busy"
    };
    fprintf(stderr, "afs: %s: %s\n", path, messages[status]);
    return 1;
}

static int report_file(const char *path) {
    fprintf(stderr, "afs: %s: %s\n", path, strerror(errno));
    return 1;
}

static FILE *open_archive(const char *path, struct kosdec_afs *afs) {
    FILE *file = fopen(path, "rb");
    if(!file) {
        report_file(path);
        return NULL;
    }
    off_t bytes;
    if(fseeko(file, 0, SEEK_END) || (bytes = ftello(file)) < 0) {
        report_file(path);
        fclose(file);
        return NULL;
    }
    enum kosdec_afs_status status = kosdec_afs_open(afs, file_read_at, file, (uint64_t)bytes);
    if(status != KOSDEC_AFS_OK) {
        report_status(path, status);
        fclose(file);
        return NULL;
    }
    return file;
}

static void print_name(const char *name) {
    for(const unsigned char *s = (const unsigned char *)name; *s; ++s) {
        if(*s < 32 || *s > 126 || *s == '\\')
            printf("\\x%02x", (unsigned)*s);
        else
            putchar(*s);
    }
}

static int list_archive(const struct kosdec_afs *afs, const char *path) {
    puts("id\toffset\tbytes\tname");
    for(uint32_t i = 0; i < afs->count; ++i) {
        struct kosdec_afs_member member;
        enum kosdec_afs_status status = kosdec_afs_get(afs, i, &member);
        if(status != KOSDEC_AFS_OK)
            return report_status(path, status);
        printf("%" PRIu32 "\t%" PRIu32 "\t%" PRIu32 "\t", member.id, member.offset, member.bytes);
        print_name(member.name);
        putchar('\n');
    }
    return ferror(stdout) ? report_file("stdout") : 0;
}

static bool copy_member(FILE *out, const struct kosdec_afs_member *member) {
    uint8_t buf[16384];
    for(uint32_t at = 0; at < member->bytes; ) {
        size_t used;
        if(kosdec_afs_read(member, at, buf, sizeof(buf), &used) != KOSDEC_AFS_OK) {
            errno = EIO;
            return false;
        }
        if(fwrite(buf, 1, used, out) != used)
            return false;
        at += (uint32_t)used;
    }
    return true;
}

static int finish_output(FILE *file, const char *path, bool ok) {
    int saved = ok ? 0 : errno;
    if(fclose(file) && !saved)
        saved = errno;
    if(ok && !saved)
        return 0;
    remove(path);
    errno = saved ? saved : EIO;
    return report_file(path);
}

static int extract_archive(const struct kosdec_afs *afs, const char *path, const char *directory) {
    size_t length = strlen(directory);
    if(length > SIZE_MAX - 32) {
        errno = ENAMETOOLONG;
        return report_file(directory);
    }
    char *output = malloc(length + 32);
    if(!output)
        return report_file("allocate output path");
    int result = 0;
    if(mkdir(directory, 0777))
        result = report_file(directory);
    for(uint32_t i = 0; !result && i < afs->count; ++i) {
        struct kosdec_afs_member member;
        enum kosdec_afs_status status = kosdec_afs_get(afs, i, &member);
        if(status != KOSDEC_AFS_OK) {
            result = report_status(path, status);
            break;
        }
        snprintf(output, length + 32, "%s/%010" PRIu32 ".bin", directory, i);
        FILE *file = fopen(output, "wbx");
        result = file ? finish_output(file, output, copy_member(file, &member)) : report_file(output);
    }
    free(output);
    return result;
}

static void store_word(uint8_t *out, uint32_t word) {
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(word >> (8u * i));
}

static bool write_pair(FILE *out, uint32_t first, uint32_t second) {
    uint8_t row[PAIR_BYTES];
    store_word(row, first);
    store_word(row + 4, second);
    return fwrite(row, 1, sizeof(row), out) == sizeof(row);
}

static uint64_t align_up(uint64_t size, unsigned alignment) {
    return (size + alignment - 1u) / alignment * alignment;
}

static bool write_padding(FILE *out, uint64_t bytes) {
    static const uint8_t zeros[KOSDEC_AFS_SECTOR_BYTES];
    while(bytes) {
        size_t chunk = bytes < sizeof(zeros) ? (size_t)bytes : sizeof(zeros);
        if(fwrite(zeros, 1, chunk, out) != chunk)
            return false;
        bytes -= chunk;
    }
    return true;
}

static bool copy_path(FILE *out, const struct item *item) {
    FILE *in = fopen(item->path, "rb");
    if(!in)
        return false;
    uint8_t bytes[16384];
    bool ok = true;
    for(uint32_t left = item->source.bytes; left; ) {
        size_t n = left < sizeof(bytes) ? left : sizeof(bytes);
        if(fread(bytes, 1, n, in) != n || fwrite(bytes, 1, n, out) != n) {
            ok = false;
            break;
        }
        left -= (uint32_t)n;
    }
    if(ok && (fgetc(in) != EOF || ferror(in)))
        ok = false;
    if(fclose(in))
        ok = false;
    if(!ok && !errno)
        errno = EIO;
    return ok;
}

static int write_archive(const char *path, struct item *items, uint32_t count, bool names,
    unsigned alignment, bool file_list, uint32_t *output_bytes) {
    uint64_t header_end = (2u + (uint64_t)count) * PAIR_BYTES;
    if(file_list && header_end > AFSLNK_HEADER) {
        errno = EFBIG;
        return report_file(path);
    }
    uint64_t first_data = align_up(file_list ? AFSLNK_HEADER : header_end, alignment);
    uint64_t location = first_data;
    uint64_t name_bytes = 0;
    for(uint32_t i = 0; i < count; ++i) {
        if(location > UINT32_MAX) {
            errno = EFBIG;
            return report_file(path);
        }
        bool hole = !items[i].path && !items[i].source.offset && !items[i].source.bytes;
        items[i].offset = hole ? 0 : (uint32_t)location;
        if(names && (!file_list || !hole))
            name_bytes += RECORD_BYTES;
        location += align_up(items[i].source.bytes, alignment);
    }
    uint64_t total_bytes = location + align_up(name_bytes, alignment);
    if(total_bytes > UINT32_MAX) {
        errno = EFBIG;
        return report_file(path);
    }

    FILE *out = fopen(path, "wbx");
    if(!out)
        return report_file(path);
    bool ok = write_pair(out, UINT32_C(0x00534641), count);
    for(uint32_t i = 0; ok && i < count; ++i)
        ok = write_pair(out, items[i].offset, items[i].source.bytes);
    uint64_t pointer_end = file_list ? AFSLNK_HEADER : header_end;
    ok = ok && write_padding(out, pointer_end - header_end) &&
         write_pair(out, name_bytes ? (uint32_t)location : 0, (uint32_t)name_bytes) &&
         write_padding(out, first_data - pointer_end);
    for(uint32_t i = 0; ok && i < count; ++i) {
        const struct item *item = &items[i];
        uint32_t bytes = item->source.bytes;
        ok = item->path ? copy_path(out, item) : copy_member(out, &item->source);
        ok = ok && write_padding(out, align_up(bytes, alignment) - bytes);
    }
    for(uint32_t i = 0; ok && names && i < count; ++i) {
        if(file_list && !items[i].offset)
            continue;
        const struct kosdec_afs_member *m = &items[i].source;
        ok = fwrite(m->name, 1, KOSDEC_AFS_NAME_BYTES, out) == KOSDEC_AFS_NAME_BYTES &&
             fwrite(m->metadata, 1, KOSDEC_AFS_METADATA_BYTES, out) == KOSDEC_AFS_METADATA_BYTES;
    }
    ok = ok && write_padding(out, total_bytes - location - name_bytes);
    int result = finish_output(out, path, ok);
    if(!result && output_bytes)
        *output_bytes = (uint32_t)total_bytes;
    return result;
}

static int pack(const char *path, char *const *inputs, uint32_t count, bool names) {
    struct item *items = calloc(count, sizeof(*items));
    if(!items)
        return report_file("allocate input table");
    int result = 0;
    for(uint32_t i = 0; !result && i < count; ++i) {
        items[i].path = inputs[i];
        struct stat info;
        if(stat(inputs[i], &info)) {
            result = report_file(inputs[i]);
            break;
        }
        if(!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX) {
            fprintf(stderr, "afs: %s: expected a regular input of at most 4 GiB minus one byte\n", inputs[i]);
            result = 1;
            break;
        }
        struct kosdec_afs_member *m = &items[i].source;
        m->bytes = (uint32_t)info.st_size;
        if(names) {
            const char *name = strrchr(inputs[i], '/');
            name = name ? name + 1 : inputs[i];
            size_t len = strlen(name);
            if(!len || len > KOSDEC_AFS_NAME_BYTES) {
                fprintf(stderr, "afs: %s: expected a basename of 1–32 bytes\n", inputs[i]);
                result = 1;
                break;
            }
            memcpy(m->name, name, len);
            store_word(m->metadata + 12, m->bytes);
        }
    }
    if(!result)
        result = write_archive(path, items, count, names, KOSDEC_AFS_SECTOR_BYTES, false, NULL);
    free(items);
    return result;
}

static int repack(const struct kosdec_afs *afs, const char *path, const char *output) {
    struct item *items = calloc(afs->count ? afs->count : 1, sizeof(*items));
    if(!items)
        return report_file("allocate member table");
    int result = 0;
    for(uint32_t i = 0; i < afs->count; ++i) {
        enum kosdec_afs_status status = kosdec_afs_get(afs, i, &items[i].source);
        if(status != KOSDEC_AFS_OK) {
            result = report_status(path, status);
            break;
        }
    }
    if(!result)
        result = write_archive(output, items, afs->count, afs->names_offset != 0,
            KOSDEC_AFS_SECTOR_BYTES, false, NULL);
    free(items);
    return result;
}


/* In the list workflow, IDs come from the base list and then appended new
   names. A replacement keeps its base ID. Only present rows carry data. */
struct list_item {
    char *path;
    char name[KOSDEC_AFS_NAME_BYTES + 1];
    bool present;
};

enum { MAX_LIST_ITEMS = AFSLNK_HEADER / PAIR_BYTES - 2, MAX_PREFIX = 64 };

struct file_list {
    struct list_item *items;
    uint32_t count, capacity;
};

static char *sidecar_path(const char *list, const char *extension) {
    const char *name = strrchr(list, '/');
    const char *dot = strrchr(name ? name + 1 : list, '.');
    size_t stem = dot ? (size_t)(dot - list) : strlen(list);
    char *path = malloc(stem + strlen(extension) + 1);
    if(path) {
        memcpy(path, list, stem);
        strcpy(path + stem, extension);
    }
    return path;
}

static void free_list(struct file_list *list) {
    for(uint32_t i = 0; i < list->count; ++i)
        free(list->items[i].path);
    free(list->items);
}

static bool append_input(struct file_list *list, const struct list_item *item) {
    if(list->count == MAX_LIST_ITEMS) {
        errno = EFBIG;
        return false;
    }
    if(list->count == list->capacity) {
        uint32_t capacity = list->capacity ? 2 * list->capacity : 32;
        if(capacity > MAX_LIST_ITEMS)
            capacity = MAX_LIST_ITEMS;
        struct list_item *grown = realloc(list->items, capacity * sizeof(*grown));
        if(!grown)
            return false;
        list->items = grown;
        list->capacity = capacity;
    }
    list->items[list->count++] = *item;
    return true;
}

static bool list_input(struct file_list *list, char *line, const char *root) {
    for(char *p = line; *p; ++p) {
        if(*p == '\\')
            *p = '/';
        if((unsigned char)*p < 32 || (unsigned char)*p > 126 || strchr("<>:\"|?*", *p)) {
            errno = EINVAL;
            return false;
        }
    }
    const char *slash = strrchr(line, '/');
    const char *name = slash ? slash + 1 : line;
    if(!*name || *line == '/' || strlen(name) > KOSDEC_AFS_NAME_BYTES) {
        errno = EINVAL;
        return false;
    }
    size_t bytes = strlen(root) + strlen(line) + 2;
    struct list_item item = {.path = malloc(bytes), .present = true};
    if(!item.path)
        return false;
    snprintf(item.path, bytes, "%s/%s", root, line);
    strcpy(item.name, name);
    if(append_input(list, &item))
        return true;
    free(item.path);
    return false;
}

static int read_list(struct file_list *list, const char *path, const char *root) {
    FILE *in = fopen(path, "rb");
    if(!in)
        return report_file(path);
    char *line = NULL;
    size_t capacity = 0;
    ssize_t count;
    int result = 0;
    while((count = getline(&line, &capacity, in)) >= 0) {
        while(count && (line[count - 1] == '\r' || line[count - 1] == '\n'))
            line[--count] = '\0';
        if(!count)
            continue;
        if(memchr(line, '\0', (size_t)count)) {
            errno = EINVAL;
            result = report_file(path);
        } else if(!list_input(list, line, root))
            result = report_file(path);
        if(result)
            break;
    }
    if(!result && (!feof(in) || !list->count)) {
        if(feof(in))
            errno = EINVAL;
        result = report_file(path);
    }
    if(fclose(in) && !result)
        result = report_file(path);
    free(line);
    return result;
}

struct id_key {
    char symbol[MAX_PREFIX + KOSDEC_AFS_NAME_BYTES + 2];
    uint32_t id;
};

static bool identifier_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void make_key(struct id_key *key, const char *prefix, const char *name, uint32_t id) {
    char *out = key->symbol;
    char first = *prefix ? *prefix : *name;
    if(first >= '0' && first <= '9')
        *out++ = '_';
    for(const char *p = prefix; *p; ++p)
        *out++ = *p;
    strcpy(out, name);
    for(out = key->symbol; *out; ++out) {
        if(!identifier_char(*out))
            *out = '_';
        else if(*out >= 'a' && *out <= 'z')
            *out = (char)(*out - 'a' + 'A');
    }
    key->id = id;
}

static int compare_keys(const void *a, const void *b) {
    const struct id_key *left = a, *right = b;
    return strcmp(left->symbol, right->symbol);
}

static struct id_key *list_keys(const struct file_list *list, const char *prefix) {
    struct id_key *keys = calloc(list->count, sizeof(*keys));
    if(!keys)
        return NULL;
    for(uint32_t i = 0; i < list->count; ++i)
        make_key(&keys[i], prefix, list->items[i].name, i);
    qsort(keys, list->count, sizeof(*keys), compare_keys);
    for(uint32_t i = 1; i < list->count; ++i) {
        if(!compare_keys(&keys[i - 1], &keys[i])) {
            fprintf(stderr, "afs: ambiguous file ID %s\n", keys[i].symbol);
            free(keys);
            errno = EINVAL;
            return NULL;
        }
    }
    return keys;
}

static bool apply_list(struct file_list *base, struct file_list *extra, const char *prefix) {
    struct id_key *keys = list_keys(base, prefix);
    if(!keys)
        return false;
    uint32_t original_count = base->count;
    for(uint32_t i = 0; i < original_count; ++i)
        base->items[i].present = false;
    bool ok = true;
    for(uint32_t i = 0; ok && i < extra->count; ++i) {
        struct list_item *item = &extra->items[i];
        struct id_key key;
        make_key(&key, prefix, item->name, i);
        const struct id_key *match = bsearch(&key, keys, original_count, sizeof(*keys), compare_keys);
        if(!match)
            ok = append_input(base, item);
        else {
            struct list_item *old = &base->items[match->id];
            /* A macro collision alone is insufficient to identify a replacement. */
            if(old->present || strcasecmp(old->name, item->name)) {
                errno = EINVAL;
                ok = false;
            } else {
                free(old->path);
                *old = *item;
            }
        }
        if(ok)
            item->path = NULL;
    }
    free(keys);
    return ok;
}

static bool describe_input(struct item *item, const struct list_item *input) {
    struct stat info;
    if(stat(input->path, &info))
        return false;
    if(!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX) {
        errno = EINVAL;
        return false;
    }
    item->path = input->path;
    item->source.bytes = (uint32_t)info.st_size;
    strcpy(item->source.name, input->name);
    struct tm date;
    if(!localtime_r(&info.st_mtime, &date))
        return false;
    const int fields[] = {date.tm_year + 1900, date.tm_mon + 1, date.tm_mday,
        date.tm_hour, date.tm_min, date.tm_sec & ~1};
    for(size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if(fields[i] < 0 || fields[i] > UINT16_MAX) {
            errno = EOVERFLOW;
            return false;
        }
        item->source.metadata[2 * i] = (uint8_t)fields[i];
        item->source.metadata[2 * i + 1] = (uint8_t)((unsigned)fields[i] >> 8);
    }
    store_word(item->source.metadata + 12, item->source.bytes);
    return true;
}

static bool write_ids(FILE *header, const struct file_list *list, const char *prefix) {
    if(fputs("/* Generated AFS file IDs */\r\n\r\n", header) == EOF)
        return false;
    for(uint32_t i = 0; i < list->count; ++i) {
        struct id_key key;
        make_key(&key, prefix, list->items[i].name, i);
        if(fprintf(header, "#define %s (%" PRIu32 ") /*  %s%s  */\r\n",
            key.symbol, i, prefix, list->items[i].name) < 0)
            return false;
    }
    return true;
}

static void discard_output(FILE *file, const char *path) {
    fclose(file);
    remove(path);
}

static int write_list_outputs(const struct file_list *list, const char *output, const char *header,
    const char *log, const char *prefix, unsigned alignment) {
    struct item *items = calloc(list->count, sizeof(*items));
    if(!items)
        return report_file(output);
    int result = 0;
    for(uint32_t i = 0; !result && i < list->count; ++i) {
        if(list->items[i].present && !describe_input(&items[i], &list->items[i]))
            result = report_file(list->items[i].path);
    }
    if(result) {
        free(items);
        return result;
    }
    FILE *ids = fopen(header, "wbx");
    if(!ids) {
        free(items);
        return report_file(header);
    }
    FILE *alg = fopen(log, "wbx");
    if(!alg) {
        result = report_file(log);
        discard_output(ids, header);
    } else {
        uint32_t bytes;
        result = write_archive(output, items, list->count, true, alignment, true, &bytes);
        if(result) {
            discard_output(ids, header);
            discard_output(alg, log);
        } else {
            int header_result = finish_output(ids, header, write_ids(ids, list, prefix));
            int log_result = finish_output(alg, log,
                fprintf(alg, "%s 0 %" PRIu32 "\r\n", output, bytes) >= 0);
            if(header_result || log_result) {
                if(!header_result)
                    remove(header);
                if(!log_result)
                    remove(log);
                remove(output);
                result = 1;
            }
        }
    }
    free(items);
    return result;
}

static int pack_list(int argc, char **argv) {
    const char *manifest = argv[0], *add = NULL, *root = ".", *prefix = "", *output = NULL;
    const char *header = NULL;
    unsigned alignment = KOSDEC_AFS_SECTOR_BYTES;
    for(int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if(!strncmp(arg, "-dir=", 5) && arg[5])
            root = arg + 5;
        else if(!strncmp(arg, "-hd=", 4))
            prefix = arg + 4;
        else if(!strncmp(arg, "-add=", 5) && arg[5])
            add = arg + 5;
        else if(!strncmp(arg, "--header=", 9) && arg[9])
            header = arg + 9;
        else if(!strncmp(arg, "-al=", 4)) {
            char *end;
            errno = 0;
            unsigned long value = strtoul(arg + 4, &end, 10);
            if(errno || *end || value < KOSDEC_AFS_SECTOR_BYTES || value > UINT32_MAX ||
               value % KOSDEC_AFS_SECTOR_BYTES)
                goto usage;
            alignment = (unsigned)value;
        } else if(*arg != '-' && !output)
            output = arg;
        else
            goto usage;
    }
    if(strlen(prefix) > MAX_PREFIX)
        goto usage;
    for(const char *p = prefix; *p; ++p) {
        if(!identifier_char(*p))
            goto usage;
    }

    char *default_output = output ? NULL : sidecar_path(add ? add : manifest, ".afs");
    char *default_header = header ? NULL : sidecar_path(manifest, ".h");
    char *log = sidecar_path(add ? add : manifest, ".alg");
    if(!output)
        output = default_output;
    if(!header)
        header = default_header;
    struct file_list base = {0}, extra = {0};
    int result = 0;
    if(!output || !header || !log)
        result = report_file(manifest);
    if(!result)
        result = read_list(&base, manifest, root);
    if(!result && add) {
        result = read_list(&extra, add, root);
        if(!result && !apply_list(&base, &extra, prefix))
            result = report_file(add);
    }
    if(!result) {
        struct id_key *keys = list_keys(&base, prefix);
        if(keys) {
            free(keys);
            result = write_list_outputs(&base, output, header, log, prefix, alignment);
        } else
            result = report_file(manifest);
    }
    free_list(&base);
    free_list(&extra);
    free(default_output);
    free(default_header);
    free(log);
    return result;

usage:
    fputs("usage: afs pack-list LIST.als [OUTPUT] [-dir=ROOT] [-al=BYTES] "
          "[-hd=PREFIX] [-add=LIST.als] [--header=PATH]\n", stderr);
    return 2;
}

int main(int argc, char **argv) {
    if(argc >= 3 && !strcmp(argv[1], "pack-list"))
        return pack_list(argc - 2, argv + 2);
    if(argc >= 4 && !strcmp(argv[1], "pack"))
        return pack(argv[2], argv + 3, (uint32_t)(argc - 3), true);
    if(argc >= 4 && !strcmp(argv[1], "pack-unnamed"))
        return pack(argv[2], argv + 3, (uint32_t)(argc - 3), false);
    bool list = argc == 3 && !strcmp(argv[1], "list");
    bool extract = argc == 4 && !strcmp(argv[1], "extract");
    bool rebuild = argc == 4 && !strcmp(argv[1], "repack");
    if(list || extract || rebuild) {
        struct kosdec_afs afs;
        FILE *in = open_archive(argv[2], &afs);
        if(!in)
            return 1;
        int result = list ? list_archive(&afs, argv[2]) :
                     extract ? extract_archive(&afs, argv[2], argv[3]) :
                               repack(&afs, argv[2], argv[3]);
        fclose(in);
        return result;
    }
    fputs("usage: afs list ARCHIVE\n"
          "       afs extract ARCHIVE NEW_DIRECTORY\n"
          "       afs pack OUTPUT INPUT...\n"
          "       afs pack-unnamed OUTPUT INPUT...\n"
          "       afs pack-list LIST.als [OUTPUT] [OPTIONS]\n"
          "       afs repack ARCHIVE OUTPUT\n", stderr);
    return 2;
}
