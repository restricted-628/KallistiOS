#include <kosdec/acx.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct source {
    uint8_t *data;
    size_t bytes, limit, calls;
    bool fail;
};

static ptrdiff_t read_at(void *context, uint64_t position, void *destination, size_t requested) {
    struct source *source = context;
    ++source->calls;
    if(source->fail)
        return -1;
    if(position >= source->bytes)
        return 0;
    size_t bytes = source->bytes - (size_t)position;
    if(bytes > requested)
        bytes = requested;
    if(bytes > source->limit)
        bytes = source->limit;
    memcpy(destination, source->data + position, bytes);
    return (ptrdiff_t)bytes;
}

static void word(uint8_t *out, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (24u - 8u * i));
}

int main(void) {
    enum { COUNT = 66, TABLE_END = 8 + 8 * COUNT, LENGTH = TABLE_END + 16 };
    uint8_t data[LENGTH] = {0};
    word(data + 4, COUNT);
    for(unsigned id = 0; id < COUNT; ++id)
        word(data + 8 + 8 * id, TABLE_END + 3);
    word(data + 12, 3);
    memcpy(data + TABLE_END + 3, "cat", 3);
    /* Last row aliases part of member zero, with an unaligned offset. */
    word(data + TABLE_END - 8, TABLE_END + 4);
    word(data + TABLE_END - 4, 2);

    struct source source = {.data = data, .bytes = sizeof(data), .limit = 3};
    struct kosdec_acx archive;
    struct kosdec_acx_member member;
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_OK);
    assert(archive.count == COUNT);
    assert(kosdec_acx_get(&archive, 0, &member) == KOSDEC_ACX_OK && member.bytes == 3);
    char output[8] = {0};
    size_t used = 99;
    assert(kosdec_acx_read(&member, 1, output, sizeof(output), &used) == KOSDEC_ACX_OK);
    assert(used == 2 && !memcmp(output, "at", 3));
    assert(kosdec_acx_get(&archive, COUNT - 1, &member) == KOSDEC_ACX_OK && member.bytes == 2);
    assert(kosdec_acx_read(&member, 0, output, sizeof(output), &used) == KOSDEC_ACX_OK && used == 2);
    assert(!memcmp(output, "at", 3));
    assert(kosdec_acx_get(&archive, 2, &member) == KOSDEC_ACX_OK && !member.bytes);
    size_t calls = source.calls;
    assert(kosdec_acx_read(&member, 0, output, 1, &used) == KOSDEC_ACX_EOF && !used);
    assert(kosdec_acx_read(&member, 1, output, 1, &used) == KOSDEC_ACX_BAD_ARGUMENT && !used);
    assert(kosdec_acx_get(&archive, COUNT, &member) == KOSDEC_ACX_NOT_FOUND && member.id == 2);
    assert(source.calls == calls);

    word(data + TABLE_END - 4, UINT32_MAX);
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_INVALID);
    assert(archive.count == COUNT);
    word(data + TABLE_END - 8, TABLE_END - 1);
    word(data + TABLE_END - 4, 0);
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_INVALID);
    assert(kosdec_acx_open(&archive, read_at, &source, TABLE_END - 1) == KOSDEC_ACX_TRUNCATED);
    word(data + 4, UINT32_MAX);
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_TRUNCATED);
    data[0] = 0x41;
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_UNSUPPORTED);
    data[0] = 0;
    source.bytes = 4;
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_TRUNCATED);
    source.fail = true;
    assert(kosdec_acx_open(&archive, read_at, &source, LENGTH) == KOSDEC_ACX_IO_ERROR);
    source.fail = false;
    source.bytes = LENGTH;
    word(data + 4, 0);
    assert(kosdec_acx_open(&archive, read_at, &source, 8) == KOSDEC_ACX_OK && !archive.count);
    puts("ACX READER PASS");
    return 0;
}
