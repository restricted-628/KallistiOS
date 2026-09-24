#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <lz4frame.h>
#include <kos/pvr_chunk_asset_lz4.h>

/* Exercise the real service loop with an observable scheduler double. The
   private entry_started flag lets us model a completed start handshake; this
   is not a test of real fiber context switching or interrupt concurrency. */
#include "../../addons/liblz4/pvr_chunk_asset_lz4_service.c"

unsigned test_irq_depth;
static unsigned callbacks, previous_callbacks;
static bool check_fairness;
static pvr_chunk_asset_lz4_job_t *requeue_job;
static pvr_chunk_asset_lz4_service_t *requeue_service;

fiber_service_t *fiber_service_add(fiber_service_executor_t *executor,
    void *stack, size_t size, void (*entry)(fiber_service_t *, void *), void *data) {
    assert(stack && size);
    executor->service.entry = entry;
    executor->service.data = data;
    return &executor->service;
}
void *fiber_service_executor_get_thread(fiber_service_executor_t *executor) {
    return executor;
}
bool fiber_service_stop_requested(fiber_service_t *service) { return service->stop; }
int fiber_service_wait(fiber_service_t *service, uint64_t timeout) {
    (void)timeout;
    service->stop = true;
    errno = ECANCELED;
    return -1;
}
int fiber_service_yield(fiber_service_t *service) {
    assert(test_irq_depth == 0);
    if(check_fairness) {
        assert(callbacks - previous_callbacks <= 1);
        previous_callbacks = callbacks;
    }
    ++service->yields;
    if(service->stop_after && service->yields == service->stop_after) {
        service->stop = true;
        errno = ECANCELED;
        return -1;
    }
    return 0;
}
int fiber_service_wake(fiber_service_t *service) { (void)service; return 0; }
void thd_pass(void) { assert(!"unexpected start-handshake wait"); }
uint64_t timer_ms_gettime64(void) { return 0; }

static uint8_t plain[140000], encoded[150000], output[140002];

static uint32_t checksum(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    for(size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for(unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1)));
    }
    return ~crc;
}

static pvr_chunk_asset_section_t fixture(size_t size, bool linked) {
    LZ4F_preferences_t prefs = LZ4F_INIT_PREFERENCES;
    pvr_chunk_asset_section_t section = {0};
    prefs.frameInfo.blockSizeID = LZ4F_max64KB;
    prefs.frameInfo.blockMode = linked ? LZ4F_blockLinked : LZ4F_blockIndependent;
    prefs.frameInfo.contentSize = size;
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    for(size_t i = 0; i < size; ++i)
        plain[i] = (uint8_t)(i * 13 + i / 37);
    size_t stored = LZ4F_compressFrame(encoded, sizeof(encoded), plain, size, &prefs);
    assert(!LZ4F_isError(stored));
    section.stored_data = encoded;
    section.stored_bytes = stored;
    section.decoded_bytes = size;
    section.decoded_crc32 = checksum(plain, size);
    section.codec = PVR_CHUNK_ASSET_CODEC_LZ4_FRAME;
    return section;
}

static void allocation_test(bool linked, size_t size, size_t budget) {
    pvr_chunk_asset_section_t section = fixture(size, linked);
    pvr_chunk_asset_lz4_progress_t progress;
    pvr_chunk_asset_lz4_state_t *state;
    size_t creation_calls;
    assert(test_alloc_live == 0);
    memset(output, 0xa5, sizeof(output));
    test_alloc_calls = 0;
    state = pvr_chunk_asset_lz4_state_create(&section, output + 1, size, NULL);
    assert(state);
    creation_calls = test_alloc_calls;
    assert(creation_calls == 4); /* wrapper, context, input and output scratch */
    for(size_t i = 0; i < size + 2; ++i)
        assert(output[i] == 0xa5);
    assert(pvr_chunk_asset_lz4_state_get_progress(state, &progress) == 0);
    assert(progress.output_bytes == 0 && !progress.complete);
    assert(progress.source_bytes == LZ4F_headerSize(encoded, section.stored_bytes));
    test_alloc_fail_at = creation_calls + 1;
    size_t previous = 0;
    int result;
    do {
        result = pvr_chunk_asset_lz4_state_step(state, budget);
        assert(result >= 0);
        assert(pvr_chunk_asset_lz4_state_get_progress(state, &progress) == 0);
        assert(progress.output_bytes - previous <= budget);
        previous = progress.output_bytes;
    } while(result == PVR_CHUNK_ASSET_LZ4_MORE);
    assert(test_alloc_calls == creation_calls);
    assert(progress.complete && progress.output_bytes == size);
    assert(progress.source_bytes == section.stored_bytes);
    assert(!memcmp(output + 1, plain, size));
    assert(output[0] == 0xa5 && output[size + 1] == 0xa5);
    pvr_chunk_asset_lz4_state_destroy(state);
    assert(test_alloc_live == 0);
    test_alloc_fail_at = 0;
    const pvr_chunk_asset_lz4_limits_t limits = {65536, 262148, 0};
    for(size_t failure = 1; failure <= creation_calls; ++failure) {
        test_alloc_calls = 0;
        test_alloc_fail_at = failure;
        memset(output, 0xa5, size + 2);
        errno = 0;
        state = pvr_chunk_asset_lz4_state_create_with_limits(
            &section, output + 1, size, NULL, &limits);
        assert(!state && errno == ENOMEM);
        assert(test_alloc_live == 0);
        for(size_t i = 0; i < size + 2; ++i)
            assert(output[i] == 0xa5);
    }
    test_alloc_fail_at = 0;
}

static void complete(pvr_chunk_asset_lz4_job_t *job, void *data) {
    pvr_chunk_asset_lz4_job_status_t status;
    (void)data;
    ++callbacks;
    assert(pvr_chunk_asset_lz4_job_get_status(job, &status) == 0);
    assert(job_terminal(status.state));
    errno = 0;
    assert(pvr_chunk_asset_lz4_job_destroy(job) == -1 && errno == EBUSY);
    if(requeue_job) {
        assert(pvr_chunk_asset_lz4_service_submit(requeue_service, requeue_job) == 0);
        requeue_job = NULL;
    }
}

static void service_test(int mode, bool stop) {
    /* 0: success, 1: integrity failure, 2: queued cancellation,
       3: partial decode interrupted by executor stop, 4: callback requeue. */
    pvr_chunk_asset_section_t section = fixture(100, false);
    fiber_service_executor_t executor = {0};
    uint8_t stack[64], destinations[5][100];
    pvr_chunk_asset_lz4_job_t *jobs[5];
    size_t count = mode == 4 ? 5 : 4;
    pvr_chunk_asset_lz4_service_t *service = pvr_chunk_asset_lz4_service_create(
        &executor, stack, sizeof(stack), 4, mode == 3 ? 7 : 100);
    assert(service);
    service->entry_started = true;
    callbacks = previous_callbacks = 0;
    check_fairness = true;
    requeue_job = NULL;
    requeue_service = service;
    executor.service.stop_after = stop ? 1 : 0;
    if(mode == 1)
        section.decoded_crc32 ^= 1;
    for(size_t i = 0; i < count; ++i) {
        jobs[i] = pvr_chunk_asset_lz4_job_create(&section, destinations[i],
                                               100, NULL, complete, NULL);
        assert(jobs[i]);
        if(i == 4) {
            errno = 0;
            assert(pvr_chunk_asset_lz4_service_submit(service, jobs[i]) == -1);
            assert(errno == EAGAIN);
            requeue_job = jobs[i];
        }
        else
            assert(pvr_chunk_asset_lz4_service_submit(service, jobs[i]) == 0);
        if(mode == 2)
            assert(pvr_chunk_asset_lz4_job_cancel(jobs[i]) == 0);
    }
    size_t prepared_calls = test_alloc_calls;
    test_alloc_fail_at = prepared_calls + 1;
    executor.service.entry(&executor.service, executor.service.data);
    assert(test_alloc_calls == prepared_calls);
    assert(callbacks == count);
    assert(executor.service.yields == (stop ? 1u : count));
    for(size_t i = 0; i < count; ++i) {
        pvr_chunk_asset_lz4_job_status_t status;
        assert(pvr_chunk_asset_lz4_job_get_status(jobs[i], &status) == 0);
        if(mode == 2 || mode == 3 || (stop && i != 0)) {
            assert(status.state == PVR_CHUNK_ASSET_LZ4_JOB_CANCELLED);
            assert(status.error == ECANCELED);
        }
        else if(mode == 1) {
            assert(status.state == PVR_CHUNK_ASSET_LZ4_JOB_FAILED);
            assert(status.error == EILSEQ);
        }
        else {
            assert(status.state == PVR_CHUNK_ASSET_LZ4_JOB_COMPLETE);
            assert(!memcmp(destinations[i], plain, 100));
        }
        assert(pvr_chunk_asset_lz4_job_destroy(jobs[i]) == 0);
    }
    assert(pvr_chunk_asset_lz4_service_destroy(service) == 0);
    assert(test_alloc_live == 0 && test_irq_depth == 0);
    test_alloc_fail_at = 0;
}

static void dictionary_test(bool linked) {
    static uint8_t dictionary_bytes[4096];
    uint32_t random = 12345;
    for(size_t i = 0; i < sizeof(dictionary_bytes); ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        dictionary_bytes[i] = (uint8_t)(random >> 24);
    }
    for(size_t i = 0; i < sizeof(plain); ++i)
        plain[i] = dictionary_bytes[i % sizeof(dictionary_bytes)];
    LZ4F_preferences_t prefs = LZ4F_INIT_PREFERENCES;
    prefs.frameInfo.blockSizeID = LZ4F_max64KB;
    prefs.frameInfo.blockMode = linked ? LZ4F_blockLinked : LZ4F_blockIndependent;
    prefs.frameInfo.dictID = 73;
    prefs.frameInfo.contentSize = sizeof(plain);
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    LZ4F_cctx *context;
    assert(!LZ4F_isError(LZ4F_createCompressionContext(&context, LZ4F_VERSION)));
    LZ4F_CDict *cdict = LZ4F_createCDict(dictionary_bytes, sizeof(dictionary_bytes));
    assert(cdict);
    size_t bytes = LZ4F_compressFrame_usingCDict(context, encoded, sizeof(encoded),
        plain, sizeof(plain), cdict, &prefs);
    assert(!LZ4F_isError(bytes) && bytes < sizeof(plain));
    LZ4F_freeCDict(cdict);
    LZ4F_freeCompressionContext(context);
    assert(test_alloc_live == 0);
    pvr_chunk_asset_section_t section = {0};
    section.stored_data = encoded;
    section.stored_bytes = bytes;
    section.decoded_bytes = sizeof(plain);
    section.decoded_crc32 = checksum(plain, sizeof(plain));
    section.codec = PVR_CHUNK_ASSET_CODEC_LZ4_FRAME;
    section.dictionary_id = 73;
    pvr_chunk_asset_lz4_dictionary_t dict = {
        dictionary_bytes, sizeof(dictionary_bytes), 73
    };
    pvr_chunk_asset_lz4_requirements_t requirements;
    assert(pvr_chunk_asset_lz4_get_requirements(&section, &dict, &requirements) == 0);
    assert(requirements.dictionary_bytes == sizeof(dictionary_bytes));
    assert(requirements.independent_blocks == !linked);
    assert(requirements.scratch_bytes == 131076u + (linked ? 131072u : 0));
    pvr_chunk_asset_lz4_limits_t limits = {
        requirements.block_bytes, requirements.scratch_bytes, !linked
    };
    pvr_chunk_asset_lz4_state_t *state = pvr_chunk_asset_lz4_state_create_with_limits(
        &section, output + 1, sizeof(plain), &dict, &limits);
    assert(state);
    size_t calls = test_alloc_calls;
    test_alloc_fail_at = calls + 1;
    int result;
    do {
        result = pvr_chunk_asset_lz4_state_step(state, 31);
        assert(result >= 0);
    } while(result == PVR_CHUNK_ASSET_LZ4_MORE);
    assert(!memcmp(output + 1, plain, sizeof(plain)));
    assert(test_alloc_calls == calls);
    pvr_chunk_asset_lz4_state_destroy(state);
    assert(test_alloc_live == 0);
    test_alloc_fail_at = 0;
}

/* Streaming compression preserves the requested advertised block size even
   for a tiny payload; compressFrame() may downsize it to fit the input. */
static pvr_chunk_asset_section_t profile_fixture(LZ4F_blockSizeID_t id,
                                                bool linked) {
    pvr_chunk_asset_section_t section = fixture(100, linked);
    LZ4F_preferences_t prefs = LZ4F_INIT_PREFERENCES;
    LZ4F_cctx *context;
    size_t bytes, result;
    prefs.frameInfo.blockSizeID = id;
    prefs.frameInfo.blockMode = linked ? LZ4F_blockLinked : LZ4F_blockIndependent;
    prefs.frameInfo.contentSize = 100;
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    prefs.autoFlush = 1;
    assert(!LZ4F_isError(LZ4F_createCompressionContext(&context, LZ4F_VERSION)));
    bytes = LZ4F_compressBegin(context, encoded, sizeof(encoded), &prefs);
    assert(!LZ4F_isError(bytes));
    result = LZ4F_compressUpdate(context, encoded + bytes, sizeof(encoded) - bytes,
                                plain, 100, NULL);
    assert(!LZ4F_isError(result));
    bytes += result;
    result = LZ4F_compressEnd(context, encoded + bytes, sizeof(encoded) - bytes, NULL);
    assert(!LZ4F_isError(result));
    bytes += result;
    LZ4F_freeCompressionContext(context);
    section.stored_bytes = bytes;
    assert(!test_alloc_live);
    return section;
}

static void reject_limit(const pvr_chunk_asset_section_t *section,
                         const pvr_chunk_asset_lz4_limits_t *limits) {
    memset(output, 0xa5, 102);
    test_alloc_calls = 0;
    /* Rejection must beat the first scratch allocation, even if it would fail. */
    test_alloc_fail_at = 3;
    errno = 0;
    assert(!pvr_chunk_asset_lz4_state_create_with_limits(
        section, output + 1, 100, NULL, limits));
    assert(errno == EFBIG && test_alloc_calls == 2 && !test_alloc_live);
    assert(test_alloc_sizes[0] < 65536 && test_alloc_sizes[1] < 65536);
    for(size_t i = 0; i < 102; ++i)
        assert(output[i] == 0xa5);
    test_alloc_calls = 0;
    test_alloc_fail_at = 4; /* job + wrapper + context before scratch */
    errno = 0;
    assert(!pvr_chunk_asset_lz4_job_create_with_limits(
        section, output + 1, 100, NULL, limits, NULL, NULL));
    assert(errno == EFBIG && test_alloc_calls == 3 && !test_alloc_live);
    for(size_t i = 0; i < 102; ++i)
        assert(output[i] == 0xa5);
    test_alloc_fail_at = 0;
}

static void limits_test(LZ4F_blockSizeID_t id, size_t block_bytes, bool linked) {
    pvr_chunk_asset_section_t section = profile_fixture(id, linked);
    pvr_chunk_asset_lz4_requirements_t requirements;
    pvr_chunk_asset_lz4_limits_t compact = PVR_CHUNK_ASSET_LZ4_COMPACT_LIMITS;
    pvr_chunk_asset_lz4_limits_t limits = {0};
    pvr_chunk_asset_lz4_state_t *state;
    test_alloc_calls = 0;
    assert(pvr_chunk_asset_lz4_get_requirements(&section, NULL, &requirements) == 0);
    assert(test_alloc_calls == 1 && test_alloc_sizes[0] < 65536 && !test_alloc_live);
    assert(requirements.stored_bytes == section.stored_bytes);
    assert(requirements.decoded_bytes == 100 && !requirements.dictionary_bytes);
    assert(requirements.block_bytes == block_bytes);
    assert(requirements.scratch_bytes == 2 * block_bytes + 4 + (linked ? 131072u : 0));
    assert(requirements.independent_blocks == !linked);

    limits.max_block_bytes = block_bytes - 1;
    reject_limit(&section, &limits);
    limits.max_block_bytes = 0;
    limits.max_scratch_bytes = requirements.scratch_bytes - 1;
    reject_limit(&section, &limits);
    if(linked) {
        limits.max_scratch_bytes = 0;
        limits.require_independent_blocks = 1;
        reject_limit(&section, &limits);
    }
    if(linked || block_bytes > 65536)
        reject_limit(&section, &compact);

    limits.max_block_bytes = block_bytes;
    limits.max_scratch_bytes = requirements.scratch_bytes;
    limits.require_independent_blocks = !linked;
    memset(output, 0xa5, 102);
    test_alloc_calls = 0;
    state = pvr_chunk_asset_lz4_state_create_with_limits(
        &section, output + 1, 100, NULL, &limits);
    assert(state && test_alloc_calls == 4);
    /* Detect estimate drift if vendored allocation behavior ever changes. */
    assert(test_alloc_sizes[2] + test_alloc_sizes[3] == requirements.scratch_bytes);
    test_alloc_fail_at = 5;
    int result;
    do {
        result = pvr_chunk_asset_lz4_state_step(state, 7);
        assert(result >= 0);
    } while(result == PVR_CHUNK_ASSET_LZ4_MORE);
    assert(test_alloc_calls == 4 && !memcmp(output + 1, plain, 100));
    assert(output[0] == 0xa5 && output[101] == 0xa5);
    pvr_chunk_asset_lz4_state_destroy(state);
    assert(!test_alloc_live);
    test_alloc_fail_at = 0;

    /* The legacy entry point and all-zero limits keep general compatibility. */
    state = pvr_chunk_asset_lz4_state_create(&section, output + 1, 100, NULL);
    assert(state);
    pvr_chunk_asset_lz4_state_destroy(state);
    limits = (pvr_chunk_asset_lz4_limits_t){0};
    pvr_chunk_asset_lz4_job_t *job = pvr_chunk_asset_lz4_job_create_with_limits(
        &section, output + 1, 100, NULL, &limits, NULL, NULL);
    assert(job && pvr_chunk_asset_lz4_job_destroy(job) == 0);
    assert(!test_alloc_live);
}

static void requirements_error_test(void) {
    pvr_chunk_asset_section_t section = fixture(100, false);
    pvr_chunk_asset_section_t bad;
    pvr_chunk_asset_lz4_requirements_t requirements, unchanged;
    memset(&unchanged, 0xa5, sizeof(unchanged));
    memcpy(&requirements, &unchanged, sizeof(requirements));
    test_alloc_calls = 0;
    test_alloc_fail_at = 1;
    assert(pvr_chunk_asset_lz4_get_requirements(&section, NULL, &requirements) == -1);
    assert(errno == ENOMEM && !test_alloc_live);
    assert(!memcmp(&requirements, &unchanged, sizeof(requirements)));
    test_alloc_fail_at = 0;
    assert(pvr_chunk_asset_lz4_get_requirements(NULL, NULL, &requirements) == -1);
    assert(errno == EINVAL);
    assert(pvr_chunk_asset_lz4_get_requirements(&section, NULL, NULL) == -1);
    assert(errno == EINVAL);
    bad = section;
    bad.dictionary_id = 73;
    assert(pvr_chunk_asset_lz4_get_requirements(&bad, NULL, &requirements) == -1);
    assert(errno == ENOENT);
    pvr_chunk_asset_lz4_dictionary_t dict = {plain, 100, 73};
    assert(pvr_chunk_asset_lz4_get_requirements(&bad, &dict, &requirements) == -1);
    assert(errno == EILSEQ);
    bad = section;
    ++bad.decoded_bytes;
    assert(pvr_chunk_asset_lz4_get_requirements(&bad, NULL, &requirements) == -1);
    assert(errno == EILSEQ);
    size_t header = LZ4F_headerSize(encoded, section.stored_bytes);
    assert(!LZ4F_isError(header));
    for(size_t size = 1; size < header; ++size) {
        bad = section;
        bad.stored_bytes = size;
        assert(pvr_chunk_asset_lz4_get_requirements(&bad, NULL, &requirements) == -1);
        assert(errno == EILSEQ && !test_alloc_live);
        assert(!memcmp(&requirements, &unchanged, sizeof(requirements)));
    }
    encoded[header - 1] ^= 1; /* Invalid header checksum, never a limit error. */
    pvr_chunk_asset_lz4_limits_t limits = {1, 1, 1};
    assert(!pvr_chunk_asset_lz4_state_create_with_limits(
        &section, output + 1, 100, NULL, &limits));
    assert(errno == EILSEQ && !test_alloc_live);
    assert(pvr_chunk_asset_lz4_get_requirements(&section, NULL, &requirements) == -1);
    assert(errno == EILSEQ && !memcmp(&requirements, &unchanged, sizeof(requirements)));
    encoded[header - 1] ^= 1;

    /* A header query is not payload admission; creation reparses the header. */
    bad = section;
    bad.stored_bytes = header;
    assert(pvr_chunk_asset_lz4_get_requirements(&bad, NULL, &requirements) == 0);
    assert(requirements.stored_bytes == header);
    limits = (pvr_chunk_asset_lz4_limits_t)PVR_CHUNK_ASSET_LZ4_COMPACT_LIMITS;
    pvr_chunk_asset_lz4_state_t *state = pvr_chunk_asset_lz4_state_create_with_limits(
        &bad, output + 1, 100, NULL, &limits);
    assert(state);
    assert(pvr_chunk_asset_lz4_state_step(state, 100) == -1 && errno == EILSEQ);
    pvr_chunk_asset_lz4_state_destroy(state);
    encoded[header - 1] ^= 1;
    assert(!pvr_chunk_asset_lz4_state_create_with_limits(
        &section, output + 1, 100, NULL, &limits));
    assert(errno == EILSEQ && !test_alloc_live);
    encoded[header - 1] ^= 1;
}

int main(void) {
    allocation_test(false, 100, 7);
    allocation_test(true, 100, 7);
    allocation_test(false, sizeof(plain), 8192);
    allocation_test(true, sizeof(plain), 8192);
    for(int mode = 0; mode < 3; ++mode) {
        service_test(mode, false);
        service_test(mode, true);
    }
    service_test(3, true);
    service_test(4, false);
    dictionary_test(false);
    dictionary_test(true);
    requirements_error_test();
    for(unsigned linked = 0; linked <= 1; ++linked) {
        limits_test(LZ4F_max64KB, 65536, linked);
        limits_test(LZ4F_max256KB, 262144, linked);
        limits_test(LZ4F_max1MB, 1048576, linked);
        limits_test(LZ4F_max4MB, 4194304, linked);
    }
    puts("LZ4 allocation, admission, failure, and service-yield tests passed");
    return 0;
}
