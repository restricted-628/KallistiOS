#include <kosdec/afs.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct source {
    uint8_t *bytes;
    size_t size, cap;
    bool error;
};

static ptrdiff_t read_part(void *context, uint64_t at, void *dst, size_t bytes) {
    struct source *s = context;
    if(s->error)
        return -1;
    if(at >= s->size)
        return 0;
    size_t left = s->size - (size_t)at;
    size_t count = bytes < left ? bytes : left;
    if(count > s->cap)
        count = s->cap;
    memcpy(dst, s->bytes + at, count);
    return (ptrdiff_t)count;
}

static void word(uint8_t *out, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (8u * i));
}

int main(void) {
    enum { HEADER = 8, TABLE_END = 40, PAYLOAD = 2048, NAME_START = 2112, COUNT = 4 };
    uint8_t data[NAME_START + COUNT * 48] = {0};
    struct source source = {.bytes = data, .size = sizeof(data), .cap = 3};
    struct kosdec_afs afs;
    struct kosdec_afs_member member;
    memcpy(data, "AFS", 3);
    word(data + 4, COUNT);
    /* Member zero is stored after member one. Member two is empty,
       and member three aliases member one. */
    word(data + HEADER, PAYLOAD + 20);
    word(data + HEADER + 4, 3);
    word(data + HEADER + 8, PAYLOAD);
    word(data + HEADER + 12, 5);
    word(data + HEADER + 24, PAYLOAD);
    word(data + HEADER + 28, 5);
    memcpy(data + PAYLOAD, "voice", 5);
    memcpy(data + PAYLOAD + 20, "OBJ", 3);

    assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_OK);
    assert(afs.count == COUNT && !afs.names_offset);
    assert(kosdec_afs_get(&afs, 4, &member) == KOSDEC_AFS_NOT_FOUND);
    assert(kosdec_afs_find(&afs, "scene", &member) == KOSDEC_AFS_NOT_FOUND);
    assert(kosdec_afs_get(&afs, 1, &member) == KOSDEC_AFS_OK && !member.has_name_table);
    char out[6] = {0};
    size_t used;
    assert(kosdec_afs_read(&member, 1, out, sizeof(out), &used) == KOSDEC_AFS_OK);
    assert(used == 4 && !strcmp(out, "oice"));
    assert(kosdec_afs_read(&member, 5, out, 1, &used) == KOSDEC_AFS_EOF && !used);
    assert(kosdec_afs_read(&member, 7, out, 1, &used) == KOSDEC_AFS_BAD_ARGUMENT && !used);
    assert(kosdec_afs_get(&afs, 2, &member) == KOSDEC_AFS_OK && !member.bytes);
    assert(kosdec_afs_read(&member, 0, out, sizeof(out), &used) == KOSDEC_AFS_EOF);

    const char full_name[] = "12345678901234567890123456789012";
    memcpy(data + NAME_START, full_name, 32);
    memcpy(data + NAME_START + 48, "scene", 5);
    memcpy(data + NAME_START + 144, "scene", 5);
    memset(data + NAME_START + 48 + 32, 0xa5, 16);
    for(unsigned pointer = TABLE_END; pointer < PAYLOAD; pointer = pointer == TABLE_END ? PAYLOAD - 8 : PAYLOAD) {
        word(data + pointer, NAME_START);
        word(data + pointer + 4, COUNT * 48);
        assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_OK);
        assert(kosdec_afs_find(&afs, full_name, &member) == KOSDEC_AFS_OK && member.id == 0);
        assert(kosdec_afs_find(&afs, "scene", &member) == KOSDEC_AFS_OK && member.id == 1);
        assert(member.metadata[0] == 0xa5 && member.metadata[15] == 0xa5);
        memset(data + pointer, 0, 8);
    }

    word(data + TABLE_END, PAYLOAD);
    word(data + TABLE_END + 4, COUNT * 48);
    assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_INVALID);
    memset(data + TABLE_END, 0, 8);
    word(data + HEADER + 4, UINT32_MAX);
    assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_INVALID);
    word(data + HEADER + 4, 3);
    assert(kosdec_afs_open(&afs, read_part, &source, 12) == KOSDEC_AFS_TRUNCATED);
    data[3] = 1;
    assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_UNSUPPORTED);
    data[3] = 0;
    source.error = true;
    assert(kosdec_afs_open(&afs, read_part, &source, sizeof(data)) == KOSDEC_AFS_IO_ERROR);
    puts("AFS READER PASS");
    return 0;
}
