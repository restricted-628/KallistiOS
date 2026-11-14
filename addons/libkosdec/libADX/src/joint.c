#include <kosdec/adx_joint.h>
#include <string.h>

#define JOINT_MAGIC UINT32_C(0x4144584a)
static int valid(const struct kosdec_adx_joint *j) {
    return j && j->magic == JOINT_MAGIC;
}
static int line_valid(enum kosdec_adx_joint_line line) {
    return line == KOSDEC_ADX_JOINT_FREE || line == KOSDEC_ADX_JOINT_DATA;
}
static enum kosdec_adx_joint_status fail(struct kosdec_adx_joint *j,
                                        enum kosdec_adx_joint_status status) {
    if(valid(j) && j->error) j->error(j->error_context, status);
    return status;
}
static size_t minimum(size_t a, size_t b) { return a < b ? a : b; }
static int busy(const struct kosdec_adx_joint *j) {
    return j->borrowed[0].bytes || j->borrowed[1].bytes;
}
static void reset(struct kosdec_adx_joint *j) {
    j->position[0] = j->position[1] = 0;
    j->available[0] = j->kind == KOSDEC_ADX_JOINT_RING ? j->capacity : 0;
    j->available[1] = j->kind == KOSDEC_ADX_JOINT_MEMORY ? j->capacity : 0;
}
static enum kosdec_adx_joint_status init(struct kosdec_adx_joint *j,
    void *data, size_t capacity, size_t extra, enum kosdec_adx_joint_kind kind) {
    if(!j || !data || !capacity || capacity > INT32_MAX || extra > capacity ||
       extra > (size_t)INT32_MAX - capacity) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    *j = (struct kosdec_adx_joint){0};
    j->magic = JOINT_MAGIC; j->kind = kind; j->buffer = data;
    j->capacity = capacity; j->extra = extra;
    reset(j);
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_memory_init(
    struct kosdec_adx_joint *j, void *data, size_t bytes) {
    return init(j, data, bytes, 0, KOSDEC_ADX_JOINT_MEMORY);
}
enum kosdec_adx_joint_status kosdec_adx_joint_ring_init(
    struct kosdec_adx_joint *j, void *data, size_t capacity, size_t extra) {
    return init(j, data, capacity, extra, KOSDEC_ADX_JOINT_RING);
}
enum kosdec_adx_joint_status kosdec_adx_joint_reset(struct kosdec_adx_joint *j) {
    if(!valid(j)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    if(busy(j)) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    reset(j);
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_close(struct kosdec_adx_joint *j) {
    if(!valid(j)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    if(busy(j)) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    *j = (struct kosdec_adx_joint){0};
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_set_error(
    struct kosdec_adx_joint *j, kosdec_adx_joint_error fn, void *context) {
    if(!valid(j)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    j->error = fn; j->error_context = fn ? context : NULL;
    return KOSDEC_ADX_JOINT_OK;
}
static size_t contiguous(const struct kosdec_adx_joint *j, unsigned line) {
    if(j->borrowed[line].bytes) return 0;
    return minimum(j->available[line], j->capacity - j->position[line] + j->extra);
}
enum kosdec_adx_joint_status kosdec_adx_joint_query(
    struct kosdec_adx_joint *j, struct kosdec_adx_joint_info *info) {
    if(!valid(j) || !info) return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    *info = (struct kosdec_adx_joint_info){.kind=j->kind,
                                        .capacity=j->capacity, .extra=j->extra};
    for(unsigned line = 0; line < 2; ++line) {
        info->available[line] = j->available[line];
        info->contiguous[line] = contiguous(j, line);
        info->borrowed[line] = j->borrowed[line].bytes;
    }
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_get(struct kosdec_adx_joint *j,
    enum kosdec_adx_joint_line line, size_t request, struct kosdec_adx_chunk *out) {
    if(!valid(j) || !line_valid(line) || !out)
        return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    if(request && j->borrowed[line].bytes) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    size_t n = minimum(request, contiguous(j, line));
    if(n && j->next_token == UINT64_MAX) return fail(j, KOSDEC_ADX_JOINT_LIMIT);
    struct kosdec_adx_chunk chunk = {0};
    if(n) {
        chunk = (struct kosdec_adx_chunk){j->buffer + j->position[line], n,
                                         j, ++j->next_token, line};
        j->borrowed[line] = chunk;
        j->available[line] -= n;
        j->position[line] += n;
        if(j->kind == KOSDEC_ADX_JOINT_RING && j->position[line] >= j->capacity)
            j->position[line] -= j->capacity;
    }
    *out = chunk;
    return KOSDEC_ADX_JOINT_OK;
}
static enum kosdec_adx_joint_status check(struct kosdec_adx_joint *j,
    const struct kosdec_adx_chunk *c, int suffix) {
    if(!valid(j) || !c) return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    if(!c->bytes) return KOSDEC_ADX_JOINT_OK;
    if(c->owner != j || !line_valid(c->line)) return fail(j, KOSDEC_ADX_JOINT_STALE);
    const struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    if(!b->bytes || b->token != c->token || c->bytes > b->bytes ||
       c->data != b->data + (suffix ? b->bytes - c->bytes : 0))
        return fail(j, KOSDEC_ADX_JOINT_STALE);
    return KOSDEC_ADX_JOINT_OK;
}
/* Only the committed written span participates in mirror maintenance. Copying
   the entire prefix could overwrite a simultaneous outstanding FREE borrow. */
static void publish(struct kosdec_adx_joint *j, const struct kosdec_adx_chunk *c) {
    size_t offset = (size_t)(c->data - j->buffer);
    if(offset < j->extra) {
        size_t n = minimum(c->bytes, j->extra - offset);
        memcpy(j->buffer + j->capacity + offset, c->data, n);
    }
    if(offset + c->bytes > j->capacity) {
        size_t end = offset + c->bytes;
        size_t begin = offset > j->capacity ? offset : j->capacity;
        memcpy(j->buffer + begin - j->capacity, j->buffer + begin, end - begin);
    }
}
enum kosdec_adx_joint_status kosdec_adx_joint_put(
    struct kosdec_adx_joint *j, const struct kosdec_adx_chunk *c) {
    enum kosdec_adx_joint_status status = check(j, c, 0);
    if(status != KOSDEC_ADX_JOINT_OK || !c->bytes) return status;
    struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    if(j->kind == KOSDEC_ADX_JOINT_RING) {
        if(c->line == KOSDEC_ADX_JOINT_FREE) publish(j, c);
        j->available[1 - c->line] += c->bytes;
    }
    b->data += c->bytes; b->bytes -= c->bytes;
    if(!b->bytes) *b = (struct kosdec_adx_chunk){0};
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_unget(
    struct kosdec_adx_joint *j, const struct kosdec_adx_chunk *c) {
    enum kosdec_adx_joint_status status = check(j, c, 1);
    if(status != KOSDEC_ADX_JOINT_OK || !c->bytes) return status;
    struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    size_t position = j->position[c->line];
    if(position < c->bytes) position += j->capacity;
    j->position[c->line] = position - c->bytes;
    j->available[c->line] += c->bytes;
    b->bytes -= c->bytes;
    if(!b->bytes) *b = (struct kosdec_adx_chunk){0};
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_chunk_split(
    const struct kosdec_adx_chunk *in, size_t prefix,
    struct kosdec_adx_chunk *first, struct kosdec_adx_chunk *last) {
    if(!in || !first || !last || first == last || (in->bytes && !in->data))
        return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    struct kosdec_adx_chunk a = *in, b = *in;
    size_t n = minimum(prefix, in->bytes);
    a.bytes = n; b.bytes -= n;
    if(b.bytes) b.data += n; else b = (struct kosdec_adx_chunk){0};
    if(!a.bytes) a = (struct kosdec_adx_chunk){0};
    *first = a; *last = b;
    return KOSDEC_ADX_JOINT_OK;
}
