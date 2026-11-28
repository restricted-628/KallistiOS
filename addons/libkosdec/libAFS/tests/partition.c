#include <kosdec/afs.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct source {
    const uint8_t *data;
    size_t bytes, limit;
    unsigned calls;
    bool wait, error;
};

static ptrdiff_t read_at(void *context, uint64_t at, void *out, size_t capacity) {
    struct source *source = context;
    ++source->calls;
    assert(capacity && capacity <= 512);
    if(source->wait)
        return KOSDEC_AFS_READ_WAIT;
    if(source->error)
        return -1;
    if(at >= source->bytes)
        return 0;
    size_t used = source->bytes - (size_t)at;
    if(used > capacity)
        used = capacity;
    if(used > source->limit)
        used = source->limit;
    memcpy(out, source->data + at, used);
    return (ptrdiff_t)used;
}

static void word(uint8_t *out, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (8u * i));
}

static void row(uint8_t *out, unsigned id, uint32_t offset, uint32_t bytes) {
    word(out + 8 + id * 8, offset);
    word(out + 12 + id * 8, bytes);
}

int main(void) {
    enum { COUNT = 67 };
    uint8_t base[8192] = "AFS", overlay[8192] = "AFS";
    word(base + 4, COUNT - 1);
    word(overlay + 4, COUNT);
    row(base, 0, 2048, 11);
    row(base, 1, 4096, 2);
    row(base, 2, 4101, 1);
    row(overlay, 1, 2048, 2049);
    row(overlay, COUNT - 1, 6144, 0);
    memcpy(base + 2048, "base source", 11);
    memcpy(overlay + 2048, "new", 3);

    struct source base_source = {base, sizeof(base), 512, 0, false, false};
    struct source overlay_source = {overlay, sizeof(overlay), 7, 0, true, false};
    struct kosdec_afs_partition original = {0}, patch = {0}, too_small = {0};
    struct kosdec_afs_registry registry = {0};
    struct kosdec_afs_range base_ranges[COUNT - 1], patch_ranges[COUNT];
    struct kosdec_afs_member member;
    struct kosdec_afs_sector_range range;
    int directory;
    size_t size = 999;

    assert(kosdec_afs_partition_work_size(COUNT) == sizeof(patch_ranges));
    assert(!kosdec_afs_partition_work_size(KOSDEC_AFS_PARTITION_MAX_MEMBERS + 1));
    assert(kosdec_afs_partition_info_size(&registry, 42, &size) == KOSDEC_AFS_NOT_FOUND && size == 999);
    assert(kosdec_afs_partition_load(&too_small, read_at, &base_source, sizeof(base), "base.afs",
        NULL, NULL, 0) == KOSDEC_AFS_NO_SPACE);
    assert(kosdec_afs_partition_step(&too_small) == KOSDEC_AFS_NO_SPACE);
    assert(kosdec_afs_partition_load(&original, read_at, &base_source, sizeof(base), "base.afs",
        NULL, base_ranges, COUNT - 1) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_bind(&registry, 42, &original) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_bind(&registry, 43, &original) == KOSDEC_AFS_BUSY);
    assert(kosdec_afs_partition_begin(&original, read_at, &base_source, sizeof(base), "other.afs",
        NULL, base_ranges, COUNT - 1) == KOSDEC_AFS_BUSY);
    assert(kosdec_afs_partition_info_size(&registry, 42, &size) == KOSDEC_AFS_OK &&
        size == sizeof(base_ranges));

    assert(kosdec_afs_partition_begin(&patch, read_at, &overlay_source, sizeof(overlay),
        "patch.afs", &directory, patch_ranges, COUNT) == KOSDEC_AFS_LOADING && !overlay_source.calls);
    assert(kosdec_afs_partition_step(&patch) == KOSDEC_AFS_WAIT && overlay_source.calls == 1);
    assert(kosdec_afs_partition_status(&patch) == KOSDEC_AFS_WAIT);
    assert(kosdec_afs_partition_add(&registry, 42, &patch) == KOSDEC_AFS_WAIT);
    assert(kosdec_afs_partition_open(&registry, 42, 1, &member) == KOSDEC_AFS_OK && member.bytes == 2);
    overlay_source.wait = false;
    do {
        unsigned calls = overlay_source.calls;
        enum kosdec_afs_status status = kosdec_afs_partition_step(&patch);
        assert(overlay_source.calls == calls + 1);
        assert(status == KOSDEC_AFS_LOADING || status == KOSDEC_AFS_OK);
        if(status == KOSDEC_AFS_OK)
            break;
    } while(true);
    assert(kosdec_afs_partition_add(&registry, 42, &patch) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_info_size(&registry, 42, &size) == KOSDEC_AFS_OK &&
        size == sizeof(patch_ranges));

    unsigned reads = overlay_source.calls + base_source.calls;
    assert(kosdec_afs_partition_sector_range(&registry, 42, 0, &range) == KOSDEC_AFS_OK &&
        !strcmp(range.filename, "base.afs") && !range.directory && range.first_sector == 1 &&
        range.sectors == 1);
    assert(kosdec_afs_partition_sector_range(&registry, 42, 1, &range) == KOSDEC_AFS_OK &&
        !strcmp(range.filename, "patch.afs") && range.directory == &directory &&
        range.first_sector == 1 && range.sectors == 2);
    assert(kosdec_afs_partition_sector_range(&registry, 42, 2, &range) == KOSDEC_AFS_UNSUPPORTED);
    assert(kosdec_afs_partition_open(&registry, 42, 1, &member) == KOSDEC_AFS_OK && member.bytes == 2049);
    struct kosdec_afs_member empty;
    assert(kosdec_afs_partition_open(&registry, 42, COUNT - 1, &empty) == KOSDEC_AFS_OK && !empty.bytes);
    assert(kosdec_afs_partition_open(&registry, 42, COUNT, &empty) == KOSDEC_AFS_NOT_FOUND);
    assert(overlay_source.calls + base_source.calls == reads);

    assert(kosdec_afs_partition_pop(&registry, 42) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_open(&registry, 42, COUNT - 1, &empty) == KOSDEC_AFS_NOT_FOUND);
    char payload[3];
    size_t got;
    assert(kosdec_afs_read(&member, 0, payload, sizeof(payload), &got) == KOSDEC_AFS_OK &&
        got == sizeof(payload) && !memcmp(payload, "new", sizeof(payload)));
    assert(kosdec_afs_partition_pop(&registry, 42) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_pop(&registry, 42) == KOSDEC_AFS_NOT_FOUND);

    assert(kosdec_afs_partition_load(&original, read_at, &base_source, sizeof(base), "base.afs",
        &directory, base_ranges, COUNT - 1) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_bind(&registry, 7, &original) == KOSDEC_AFS_OK);
    assert(kosdec_afs_partition_sector_range(&registry, 7, 0, &range) == KOSDEC_AFS_OK &&
        range.directory == &directory && !strcmp(range.filename, "base.afs"));
    assert(kosdec_afs_partition_pop(&registry, 7) == KOSDEC_AFS_OK);

    assert(kosdec_afs_partition_begin(&patch, read_at, &overlay_source, sizeof(overlay),
        "patch.afs", NULL, patch_ranges, COUNT) == KOSDEC_AFS_LOADING);
    overlay_source.error = true;
    assert(kosdec_afs_partition_step(&patch) == KOSDEC_AFS_IO_ERROR);
    overlay_source.error = false;
    assert(kosdec_afs_partition_step(&patch) == KOSDEC_AFS_IO_ERROR);
    row(overlay, COUNT - 1, 16, 1);
    assert(kosdec_afs_partition_load(&patch, read_at, &overlay_source, sizeof(overlay), "bad.afs",
        NULL, patch_ranges, COUNT) == KOSDEC_AFS_INVALID);
    assert(kosdec_afs_partition_bind(&registry, 42, &patch) == KOSDEC_AFS_INVALID);
    puts("AFS PARTITION PASS");
    return 0;
}
