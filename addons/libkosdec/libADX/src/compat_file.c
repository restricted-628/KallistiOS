#include <kosdec/adx_compat_file.h>
#include <errno.h>
#include <string.h>

#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES
#define RETRY_FAILURE_LIMIT 12u

static int fail(int error) {
    errno = error;
    return -1;
}

static bool open_file(const struct kosdec_adx_compat_file *file) {
    return file && file->request.read_at;
}

static bool valid_set(const struct kosdec_adx_compat_files *files) {
    return files && files->slots && files->capacity && files->capacity <= KOSDEC_ADX_COMPAT_MAX_FILES;
}

int kosdec_adx_compat_files_init(struct kosdec_adx_compat_files *files,
    struct kosdec_adx_compat_file *slots, unsigned capacity) {
    struct kosdec_adx_compat_files next = {slots, capacity};
    if(!files || !valid_set(&next)) return fail(EINVAL);
    memset(slots, 0, capacity * sizeof(*slots));
    *files = next;
    return 0;
}

static struct kosdec_adx_input_result read_file(void *context, uint32_t offset,
                                               uint8_t *output, size_t bytes) {
    struct kosdec_adx_compat_file *file = context;
    struct kosdec_adx_input_result result = file->read_at(file->context, offset, output, bytes);
    file->retryable = result.status == KOSDEC_ADX_INPUT_ERROR && !result.bytes &&
                      file->retry_error && file->retry_error(file->context, offset);
    return result;
}

int kosdec_adx_compat_file_open(struct kosdec_adx_compat_file *file,
    const struct kosdec_adx_source *source) {
    if(!file || !source || !source->read_at) return fail(EINVAL);
    if(open_file(file)) return fail(EBUSY);
    memset(file, 0, sizeof(*file));
    file->read_at = source->read_at;
    file->context = source->context;
    kosdec_adx_request_init(&file->request, read_file, file, source->bytes);
    file->next_limit = 512;
    return 0;
}

int kosdec_adx_compat_file_set_batch(struct kosdec_adx_compat_file *file, uint32_t sectors) {
    if(!open_file(file) || !sectors) return fail(EINVAL);
    file->next_limit = sectors;
    return 0;
}

int kosdec_adx_compat_file_set_retry(struct kosdec_adx_compat_file *file,
    kosdec_adx_compat_retry_error retry_error) {
    if(!open_file(file)) return fail(EINVAL);
    if(file->info.state == KOSDEC_ADX_REQUEST_READING) return fail(EBUSY);
    file->retry_error = retry_error;
    return 0;
}

static uint32_t admitted(const struct kosdec_adx_compat_file *file, uint32_t sectors) {
    uint32_t remaining = file->request.sectors - file->info.cursor;
    return sectors < remaining ? sectors : remaining;
}

static int begin(struct kosdec_adx_compat_file *file, uint32_t count,
                 uint8_t *output, struct kosdec_adx_joint *joint) {
    if(kosdec_adx_request_seek(&file->request, file->info.cursor, KOSDEC_ADX_REQUEST_SET) !=
       KOSDEC_ADX_REQUEST_STOPPED)
        return fail(EIO);
    file->joint = joint;
    file->output = output;
    file->active_limit = file->next_limit;
    file->read_errors = 0;
    file->info.start = file->info.cursor;
    file->info.requested = count;
    file->info.completed = file->info.transferred = 0;
    file->info.state = KOSDEC_ADX_REQUEST_READING;
    file->info.transfer = count ? KOSDEC_ADX_REQUEST_READING : KOSDEC_ADX_REQUEST_READEND;
    return 0;
}

int kosdec_adx_compat_file_start(struct kosdec_adx_compat_file *file, uint32_t sectors,
    uint8_t *output, size_t capacity) {
    if(!open_file(file)) return fail(EINVAL);
    if(file->info.state == KOSDEC_ADX_REQUEST_READING) return fail(EBUSY);
    uint32_t count = admitted(file, sectors);
    if((uint64_t)count * SECTOR > capacity || (count && !output)) return fail(EINVAL);
    return begin(file, count, output, NULL);
}

int kosdec_adx_compat_file_start_joint(struct kosdec_adx_compat_file *file, uint32_t sectors,
    struct kosdec_adx_joint *joint) {
    if(!open_file(file)) return fail(EINVAL);
    if(file->info.state == KOSDEC_ADX_REQUEST_READING) return fail(EBUSY);
    struct kosdec_adx_joint_info info;
    if(kosdec_adx_joint_query(joint, &info) != KOSDEC_ADX_JOINT_OK ||
       info.kind == KOSDEC_ADX_JOINT_MEMORY || info.sealed ||
       (info.kind == KOSDEC_ADX_JOINT_RING && info.capacity < SECTOR))
        return fail(EINVAL);
    if(info.borrowed[KOSDEC_ADX_JOINT_FREE]) return fail(EBUSY);
    return begin(file, admitted(file, sectors), NULL, joint);
}

static int return_free(struct kosdec_adx_compat_file *file) {
    if(file->held.bytes && kosdec_adx_joint_unget(file->joint, &file->held) != KOSDEC_ADX_JOINT_OK)
        return fail(EIO);
    file->held = (struct kosdec_adx_chunk){0};
    return 0;
}

static int release(struct kosdec_adx_compat_file *file) {
    if(kosdec_adx_request_stop(&file->request) != KOSDEC_ADX_REQUEST_STOPPED)
        return fail(EIO);
    if(return_free(file) < 0) return -1;
    file->output = NULL;
    file->joint = NULL;
    return 0;
}

int kosdec_adx_compat_file_stop(struct kosdec_adx_compat_file *file) {
    if(!open_file(file)) return fail(EINVAL);
    if(file->info.state == KOSDEC_ADX_REQUEST_READING) {
        if(release(file) < 0) return -1;
        file->info.completed = file->info.transferred;
    }
    file->info.transfer = file->info.state = KOSDEC_ADX_REQUEST_STOPPED;
    return 0;
}

int kosdec_adx_compat_file_seek(struct kosdec_adx_compat_file *file,
    int64_t offset, enum kosdec_adx_request_origin origin) {
    if(!open_file(file) || (origin != KOSDEC_ADX_REQUEST_SET &&
       origin != KOSDEC_ADX_REQUEST_CUR && origin != KOSDEC_ADX_REQUEST_END))
        return fail(EINVAL);
    if(kosdec_adx_compat_file_stop(file) < 0) return -1;
    if(kosdec_adx_request_seek(&file->request, file->info.cursor, KOSDEC_ADX_REQUEST_SET) !=
           KOSDEC_ADX_REQUEST_STOPPED ||
       kosdec_adx_request_seek(&file->request, offset, origin) != KOSDEC_ADX_REQUEST_STOPPED)
        return fail(EIO);
    file->info.cursor = file->request.cursor;
    return 0;
}

int kosdec_adx_compat_file_get_info(const struct kosdec_adx_compat_file *file,
    struct kosdec_adx_compat_file_info *out) {
    if(!open_file(file) || !out) return fail(EINVAL);
    *out = file->info;
    return 0;
}

int kosdec_adx_compat_file_close(struct kosdec_adx_compat_file *file) {
    if(kosdec_adx_compat_file_stop(file) < 0) return -1;
    memset(file, 0, sizeof(*file));
    return 0;
}

/* Borrow the largest eligible whole-sector prefix. A SEPARATE queue loans
   the whole head, so return its unused suffix before admitting the transfer. */
static int reserve(struct kosdec_adx_compat_file *file, uint32_t *count) {
    struct kosdec_adx_joint_info info;
    if(kosdec_adx_joint_query(file->joint, &info) != KOSDEC_ADX_JOINT_OK || info.sealed)
        return fail(EIO);
    size_t free = info.contiguous[KOSDEC_ADX_JOINT_FREE];
    if(free / SECTOR < *count) *count = (uint32_t)(free / SECTOR);
    if(!*count) return 0;
    size_t bytes = (size_t)*count * SECTOR;
    bool separate = info.kind == KOSDEC_ADX_JOINT_QUEUE && info.mode == KOSDEC_ADX_JOINT_SEPARATE;
    if(separate && free > bytes && !info.spare_nodes) return 0;
    enum kosdec_adx_joint_status result = kosdec_adx_joint_get(file->joint,
        KOSDEC_ADX_JOINT_FREE, separate ? free : bytes, &file->held);
    if(result == KOSDEC_ADX_JOINT_LIMIT) return 0;
    if(result != KOSDEC_ADX_JOINT_OK || file->held.bytes < bytes) return fail(EIO);
    if(file->held.bytes > bytes) {
        struct kosdec_adx_chunk prefix, suffix;
        kosdec_adx_chunk_split(&file->held, bytes, &prefix, &suffix);
        if(kosdec_adx_joint_unget(file->joint, &suffix) != KOSDEC_ADX_JOINT_OK)
            return fail(EIO);
        file->held = prefix;
    }
    return 1;
}

static int start_batch(struct kosdec_adx_compat_file *file, uint32_t count) {
    uint8_t *output = file->joint ? file->held.data :
        file->output + (size_t)file->info.transferred * SECTOR;
    if(kosdec_adx_request_start(&file->request, count, output, (size_t)count * SECTOR) !=
       KOSDEC_ADX_REQUEST_READING)
        return fail(EIO);
    return 0;
}

static int issue(struct kosdec_adx_compat_file *file) {
    uint32_t count = file->info.requested - file->info.transferred;
    if(count > file->active_limit) count = file->active_limit;
    if(file->joint) {
        int ready = reserve(file, &count);
        if(ready <= 0) return ready;
    }
    return start_batch(file, count);
}

static int advance(struct kosdec_adx_compat_file *file) {
    if(file->request.state != KOSDEC_ADX_REQUEST_READING) return issue(file);
    for(unsigned i = 0; i < SECTOR / 512u; ++i) {
        enum kosdec_adx_request_status status = kosdec_adx_request_step(&file->request);
        if(status == KOSDEC_ADX_REQUEST_READING) continue;
        if(status == KOSDEC_ADX_REQUEST_WAIT) return 0;
        if(status != KOSDEC_ADX_REQUEST_READEND) {
            if(status == KOSDEC_ADX_REQUEST_IO_ERROR && file->retryable &&
               ++file->read_errors < RETRY_FAILURE_LIMIT) {
                if(kosdec_adx_request_seek(&file->request, file->request.start,
                                           KOSDEC_ADX_REQUEST_SET) != KOSDEC_ADX_REQUEST_STOPPED)
                    return fail(EIO);
                return start_batch(file, file->request.requested);
            }
            file->info.transfer = status;
            return 0;
        }
        if(file->joint && kosdec_adx_joint_put(file->joint, &file->held) != KOSDEC_ADX_JOINT_OK)
            return fail(EIO);
        file->held = (struct kosdec_adx_chunk){0};
        file->read_errors = 0;
        file->info.transferred += file->request.completed;
        if(file->info.transferred == file->info.requested) {
            file->info.transfer = KOSDEC_ADX_REQUEST_READEND;
            return 0;
        }
        return issue(file);
    }
    return 0;
}

static int observe(struct kosdec_adx_compat_file *file) {
    if(file->info.transfer != KOSDEC_ADX_REQUEST_READING) {
        if(release(file) < 0) return -1;
        file->info.cursor = file->info.start + file->info.transferred;
        file->info.state = file->info.transfer;
        file->info.transfer = KOSDEC_ADX_REQUEST_STOPPED;
    }
    file->info.completed = file->info.transferred;
    return 0;
}

int kosdec_adx_compat_files_observe(void *context) {
    struct kosdec_adx_compat_files *files = context;
    if(!valid_set(files)) return fail(EINVAL);
    int first_error = 0;
    for(unsigned i = 0; i < files->capacity; ++i) {
        struct kosdec_adx_compat_file *file = &files->slots[i];
        if(file->info.state == KOSDEC_ADX_REQUEST_READING && observe(file) < 0 && !first_error)
            first_error = errno;
    }
    return first_error ? fail(first_error) : 0;
}

int kosdec_adx_compat_files_stream(void *context) {
    struct kosdec_adx_compat_files *files = context;
    if(!valid_set(files)) return fail(EINVAL);
    int first_error = 0;
    for(unsigned i = 0; i < files->capacity; ++i) {
        struct kosdec_adx_compat_file *file = &files->slots[i];
        if(file->info.transfer == KOSDEC_ADX_REQUEST_READING && advance(file) < 0) {
            file->info.transfer = KOSDEC_ADX_REQUEST_IO_ERROR;
            if(!first_error) first_error = errno;
        }
    }
    return first_error ? fail(first_error) : 0;
}
