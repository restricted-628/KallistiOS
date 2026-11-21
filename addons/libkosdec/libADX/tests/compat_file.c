#include <kosdec/adx_compat_file.h>
#include <kosdec/adx_compat_service.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr, "compat_file:%d: %s\n", __LINE__, #x); exit(1); \
} } while(0)

enum service_phase { OUTSIDE, FILE_DONE, STREAM_DONE };
struct input {
    enum service_phase *phase;
    uint32_t extent, wait_at, fail_at;
    unsigned calls, waits, classified;
};
struct fixture {
    struct kosdec_adx_compat_service service;
    struct kosdec_adx_compat_title title;
    struct kosdec_adx_compat_files files;
    struct kosdec_adx_compat_file slots[3];
    enum service_phase phase;
    struct input input;
};

static uint8_t sample(uint32_t offset) {
    return (uint8_t)(offset / SECTOR + (offset & 255u) * 13u + 11u);
}
static struct kosdec_adx_input_result read_at(void *context, uint32_t offset,
                                            uint8_t *buffer, size_t bytes) {
    struct input *input = context;
    CHECK(*input->phase == FILE_DONE);
    CHECK(bytes && bytes <= 512 && offset <= input->extent && bytes <= input->extent - offset);
    ++input->calls;
    if(offset == input->wait_at) {
        ++input->waits;
        input->wait_at = UINT32_MAX;
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_WAIT, 0};
    }
    if(offset == input->fail_at)
        return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_ERROR, 0};
    for(size_t i = 0; i < bytes; ++i) buffer[i] = sample(offset + (uint32_t)i);
    return (struct kosdec_adx_input_result){KOSDEC_ADX_INPUT_DATA, bytes};
}

static uint64_t clock_us(void *unused) { (void)unused; return 0; }
static int file_phase(void *context) {
    struct fixture *f = context;
    CHECK(f->phase == OUTSIDE);
    f->phase = FILE_DONE;
    return kosdec_adx_compat_files_observe(&f->files);
}
static int stream_phase(void *context) {
    struct fixture *f = context;
    CHECK(f->phase == FILE_DONE);
    int result = kosdec_adx_compat_files_stream(&f->files);
    f->phase = STREAM_DONE;
    return result;
}

static void init(struct fixture *f) {
    memset(f, 0, sizeof(*f));
    f->input = (struct input){&f->phase, 4 * SECTOR + 5, UINT32_MAX, UINT32_MAX, 0, 0, 0};
    struct kosdec_adx_source source = {read_at, &f->input, f->input.extent, 0};
    struct kosdec_adx_compat_config config = {
        .clock_us = clock_us,
        .phases = {
            [KOSDEC_ADX_COMPAT_FILES] = {file_phase, f},
            [KOSDEC_ADX_COMPAT_STREAMS] = {stream_phase, f},
        },
    };
    CHECK(!kosdec_adx_compat_files_init(&f->files, f->slots, 3));
    CHECK(!kosdec_adx_compat_file_open(&f->slots[0], &source));
    CHECK(kosdec_adx_compat_file_open(&f->slots[0], &source) == -1 && errno == EBUSY);
    CHECK(!kosdec_adx_compat_file_set_batch(&f->slots[0], 1));
    CHECK(!kosdec_adx_compat_service_init(&f->service, &f->title, 1, &config));
}

static void pass(struct fixture *f) {
    struct kosdec_adx_compat_result result;
    f->phase = OUTSIDE;
    CHECK(!kosdec_adx_compat_exec_server(&f->service, &result));
    CHECK(f->phase == STREAM_DONE && !result.visited && !result.failed_phases);
    CHECK(result.serviced_phases == ((1u << KOSDEC_ADX_COMPAT_FILES) | (1u << KOSDEC_ADX_COMPAT_STREAMS)));
}

static void expect(struct kosdec_adx_compat_file *file, enum kosdec_adx_request_status state,
                   enum kosdec_adx_request_status transfer, uint32_t cursor,
                   uint32_t completed, uint32_t transferred) {
    struct kosdec_adx_compat_file_info info;
    CHECK(!kosdec_adx_compat_file_get_info(file, &info));
    CHECK(info.state == state && info.transfer == transfer && info.cursor == cursor &&
          info.completed == completed && info.transferred == transferred);
}

static void joint_bytes(struct kosdec_adx_joint *joint, size_t data, size_t held) {
    struct kosdec_adx_joint_info info;
    CHECK(kosdec_adx_joint_query(joint, &info) == KOSDEC_ADX_JOINT_OK);
    CHECK(info.available[KOSDEC_ADX_JOINT_DATA] == data && info.borrowed[KOSDEC_ADX_JOINT_FREE] == held);
}

static void verify_sector(const uint8_t *data, uint32_t offset, uint32_t source_bytes) {
    for(unsigned i = 0; i < SECTOR; ++i)
        CHECK(data[i] == (offset + i < source_bytes ? sample(offset + i) : 0));
}

static void joint_scope(void) {
    struct fixture f;
    init(&f);
    struct kosdec_adx_compat_file *file = &f.slots[0];
    struct kosdec_adx_joint joint;
    uint8_t bytes[4 * SECTOR];
    CHECK(kosdec_adx_joint_ring_init(&joint, bytes, sizeof(bytes), 0) == KOSDEC_ADX_JOINT_OK);
    CHECK(!kosdec_adx_compat_file_start_joint(file, 2, &joint));
    CHECK(!kosdec_adx_compat_file_set_batch(file, 2)); /* Next request only. */
    f.input.wait_at = 512;
    pass(&f);
    CHECK(!f.input.calls);
    joint_bytes(&joint, 0, SECTOR);
    pass(&f);
    CHECK(f.input.calls == 2 && f.input.waits == 1);
    joint_bytes(&joint, 0, SECTOR);
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READING, 0, 0, 1);
    joint_bytes(&joint, SECTOR, SECTOR);
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READEND, 0, 1, 2);
    joint_bytes(&joint, 2 * SECTOR, 0);
    CHECK(kosdec_adx_compat_file_start_joint(file, 1, &joint) == -1 && errno == EBUSY);
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_STOPPED, 2, 2, 2);
    unsigned calls = f.input.calls;
    pass(&f);
    CHECK(f.input.calls == calls);
    joint_bytes(&joint, 2 * SECTOR, 0);
    verify_sector(bytes, 0, f.input.extent);
    verify_sector(bytes + SECTOR, SECTOR, f.input.extent);

    /* Stop with one completed batch and the next partly written but unpublished. */
    CHECK(kosdec_adx_joint_reset(&joint) == KOSDEC_ADX_JOINT_OK);
    CHECK(!kosdec_adx_compat_file_set_batch(file, 1));
    CHECK(!kosdec_adx_compat_file_start_joint(file, 2, &joint));
    pass(&f);
    pass(&f);
    f.input.wait_at = 3 * SECTOR + 512;
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READING, 2, 1, 1);
    CHECK(kosdec_adx_joint_close(&joint) == KOSDEC_ADX_JOINT_BUSY);
    CHECK(!kosdec_adx_compat_file_stop(file));
    expect(file, KOSDEC_ADX_REQUEST_STOPPED, KOSDEC_ADX_REQUEST_STOPPED, 2, 1, 1);
    calls = f.input.calls;
    pass(&f);
    CHECK(f.input.calls == calls);
    joint_bytes(&joint, SECTOR, 0);
    verify_sector(bytes, 2 * SECTOR, f.input.extent);
    CHECK(!kosdec_adx_compat_file_close(file));
    CHECK(kosdec_adx_joint_close(&joint) == KOSDEC_ADX_JOINT_OK);
}

static void flat_scope(void) {
    struct fixture f;
    init(&f);
    struct kosdec_adx_compat_file *first = &f.slots[0], *third = &f.slots[2];
    struct input other = {&f.phase, SECTOR, UINT32_MAX, UINT32_MAX, 0, 0, 0};
    struct kosdec_adx_source source = {read_at, &other, other.extent, 0};
    CHECK(!kosdec_adx_compat_file_open(third, &source));
    uint8_t a[2 * SECTOR], b[SECTOR];
    CHECK(!kosdec_adx_compat_file_seek(first, 1, KOSDEC_ADX_REQUEST_SET));
    CHECK(!kosdec_adx_compat_file_start(first, 1, a, sizeof(a)));
    CHECK(!kosdec_adx_compat_file_start(third, 1, b, sizeof(b)));
    pass(&f);
    pass(&f);
    expect(first, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READEND, 1, 0, 1);
    expect(third, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READEND, 0, 0, 1);
    CHECK(!kosdec_adx_compat_file_stop(first));
    expect(first, KOSDEC_ADX_REQUEST_STOPPED, KOSDEC_ADX_REQUEST_STOPPED, 1, 1, 1);
    pass(&f);
    expect(third, KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_STOPPED, 1, 1, 1);
    verify_sector(b, 0, other.extent);

    f.input.fail_at = 2 * SECTOR;
    CHECK(!kosdec_adx_compat_file_start(first, 2, a, sizeof(a)));
    pass(&f);
    pass(&f);
    pass(&f);
    expect(first, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_IO_ERROR, 1, 1, 1);
    pass(&f);
    expect(first, KOSDEC_ADX_REQUEST_IO_ERROR, KOSDEC_ADX_REQUEST_STOPPED, 2, 1, 1);
    verify_sector(a, SECTOR, f.input.extent);

    CHECK(!kosdec_adx_compat_file_seek(first, INT64_MAX, KOSDEC_ADX_REQUEST_CUR));
    CHECK(!kosdec_adx_compat_file_start(first, 3, NULL, 0));
    pass(&f);
    expect(first, KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_STOPPED, 5, 0, 0);
    CHECK(!kosdec_adx_compat_file_seek(first, -1, KOSDEC_ADX_REQUEST_END));
    CHECK(kosdec_adx_compat_file_start(first, 9, a, SECTOR - 1) == -1 && errno == EINVAL);
    CHECK(!kosdec_adx_compat_file_start(first, 9, a, SECTOR));
    pass(&f);
    pass(&f);
    pass(&f);
    pass(&f);
    expect(first, KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_STOPPED, 5, 1, 1);
    verify_sector(a, 4 * SECTOR, f.input.extent);
    CHECK(!kosdec_adx_compat_file_seek(first, INT64_MIN, KOSDEC_ADX_REQUEST_CUR));
    CHECK(kosdec_adx_compat_file_seek(first, 1, (enum kosdec_adx_request_origin)9) == -1 && errno == EINVAL);
    expect(first, KOSDEC_ADX_REQUEST_STOPPED, KOSDEC_ADX_REQUEST_STOPPED, 0, 1, 1);
    CHECK(!kosdec_adx_compat_file_close(first));
    CHECK(!kosdec_adx_compat_file_close(third));
}

static bool classify_retry(void *context, uint32_t offset) {
    struct input *input = context;
    CHECK(*input->phase == FILE_DONE && offset == input->fail_at);
    ++input->classified;
    return true;
}

static void retry_scope(void) {
    struct fixture f;
    init(&f);
    struct kosdec_adx_compat_file *file = &f.slots[0];
    struct kosdec_adx_joint joint;
    uint8_t bytes[2 * SECTOR];
    CHECK(kosdec_adx_joint_ring_init(&joint, bytes, sizeof(bytes), 0) == KOSDEC_ADX_JOINT_OK);
    CHECK(!kosdec_adx_compat_file_set_retry(file, classify_retry));
    CHECK(!kosdec_adx_compat_file_start_joint(file, 2, &joint));
    CHECK(kosdec_adx_compat_file_set_retry(file, NULL) == -1 && errno == EBUSY);
    pass(&f);
    f.input.fail_at = 512;
    for(unsigned i = 0; i < 11; ++i) {
        pass(&f);
        expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READING, 0, 0, 0);
        joint_bytes(&joint, 0, SECTOR);
    }
    f.input.fail_at = UINT32_MAX;
    pass(&f);
    joint_bytes(&joint, SECTOR, SECTOR);
    f.input.fail_at = SECTOR + 512;
    for(unsigned i = 0; i < 11; ++i) {
        pass(&f);
        expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_READING, 0, 1, 1);
        joint_bytes(&joint, SECTOR, SECTOR);
    }
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READING, KOSDEC_ADX_REQUEST_IO_ERROR, 0, 1, 1);
    CHECK(f.input.classified == 23 && f.input.calls == 50);
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_IO_ERROR, KOSDEC_ADX_REQUEST_STOPPED, 1, 1, 1);
    joint_bytes(&joint, SECTOR, 0);
    verify_sector(bytes, 0, f.input.extent);
    CHECK(!kosdec_adx_compat_file_close(file));
    CHECK(kosdec_adx_joint_close(&joint) == KOSDEC_ADX_JOINT_OK);
}

static void separate_queue(void) {
    struct fixture f;
    init(&f);
    struct kosdec_adx_compat_file *file = &f.slots[0];
    struct kosdec_adx_joint joint;
    struct kosdec_adx_joint_node nodes[2];
    uint8_t bytes[2 * SECTOR];
    CHECK(kosdec_adx_joint_queue_init(&joint, nodes, 2, KOSDEC_ADX_JOINT_SEPARATE) == KOSDEC_ADX_JOINT_OK);
    CHECK(kosdec_adx_joint_seed(&joint, KOSDEC_ADX_JOINT_FREE, bytes, sizeof(bytes)) == KOSDEC_ADX_JOINT_OK);
    CHECK(!kosdec_adx_compat_file_start_joint(file, 2, &joint));
    pass(&f);
    joint_bytes(&joint, 0, SECTOR);
    pass(&f);
    joint_bytes(&joint, SECTOR, SECTOR);
    pass(&f);
    joint_bytes(&joint, 2 * SECTOR, 0);
    pass(&f);
    expect(file, KOSDEC_ADX_REQUEST_READEND, KOSDEC_ADX_REQUEST_STOPPED, 2, 2, 2);
    verify_sector(bytes, 0, f.input.extent);
    verify_sector(bytes + SECTOR, SECTOR, f.input.extent);
    CHECK(!kosdec_adx_compat_file_close(file));
    CHECK(kosdec_adx_joint_close(&joint) == KOSDEC_ADX_JOINT_OK);
}

int main(void) {
    joint_scope();
    flat_scope();
    separate_queue();
    retry_scope();
    printf("ADX_COMPAT_FILE_PASS checks=%u\n", checks);
    return 0;
}
