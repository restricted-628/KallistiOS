#define _FILE_OFFSET_BITS 64
#include <kosdec/afs.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { PAIR_BYTES = 8, RECORD_BYTES = KOSDEC_AFS_NAME_BYTES + KOSDEC_AFS_METADATA_BYTES };
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
        "invalid archive", "truncated archive", "unsupported AFS signature", "read error"
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

static uint64_t sector(uint64_t size) {
    return (size + KOSDEC_AFS_SECTOR_BYTES - 1u) & ~(uint64_t)(KOSDEC_AFS_SECTOR_BYTES - 1u);
}

static bool write_padding(FILE *out, uint64_t bytes) {
    static const uint8_t zeros[KOSDEC_AFS_SECTOR_BYTES];
    return fwrite(zeros, 1, (size_t)bytes, out) == bytes;
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

static int write_archive(const char *path, struct item *items, uint32_t count, bool names) {
    uint64_t header_end = (2u + (uint64_t)count) * PAIR_BYTES;
    uint64_t location = sector(header_end);
    uint64_t name_bytes = names ? (uint64_t)count * RECORD_BYTES : 0;
    for(uint32_t i = 0; i < count; ++i) {
        if(location > UINT32_MAX) {
            errno = EFBIG;
            return report_file(path);
        }
        bool hole = !items[i].path && !items[i].source.offset && !items[i].source.bytes;
        items[i].offset = hole ? 0 : (uint32_t)location;
        location = sector(location + items[i].source.bytes);
    }
    if(sector(location + name_bytes) > UINT32_MAX) {
        errno = EFBIG;
        return report_file(path);
    }

    FILE *out = fopen(path, "wbx");
    if(!out)
        return report_file(path);
    bool ok = write_pair(out, UINT32_C(0x00534641), count);
    for(uint32_t i = 0; ok && i < count; ++i)
        ok = write_pair(out, items[i].offset, items[i].source.bytes);
    ok = ok && write_pair(out, name_bytes ? (uint32_t)location : 0, (uint32_t)name_bytes) &&
         write_padding(out, sector(header_end) - header_end);
    for(uint32_t i = 0; ok && i < count; ++i) {
        const struct item *item = &items[i];
        uint32_t bytes = item->source.bytes;
        ok = item->path ? copy_path(out, item) : copy_member(out, &item->source);
        ok = ok && write_padding(out, sector(bytes) - bytes);
    }
    for(uint32_t i = 0; ok && names && i < count; ++i) {
        const struct kosdec_afs_member *m = &items[i].source;
        ok = fwrite(m->name, 1, KOSDEC_AFS_NAME_BYTES, out) == KOSDEC_AFS_NAME_BYTES &&
             fwrite(m->metadata, 1, KOSDEC_AFS_METADATA_BYTES, out) == KOSDEC_AFS_METADATA_BYTES;
    }
    ok = ok && write_padding(out, sector(name_bytes) - name_bytes);
    return finish_output(out, path, ok);
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
        result = write_archive(path, items, count, names);
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
        result = write_archive(output, items, afs->count, afs->names_offset != 0);
    free(items);
    return result;
}

int main(int argc, char **argv) {
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
          "       afs repack ARCHIVE OUTPUT\n", stderr);
    return 2;
}
