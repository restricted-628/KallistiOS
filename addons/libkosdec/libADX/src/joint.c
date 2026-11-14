#include <kosdec/adx_joint.h>
#include <string.h>

#define JOINT_MAGIC UINT32_C(0x4144584a)
static int valid(const struct kosdec_adx_joint *j) {
    return j && j->magic == JOINT_MAGIC;
}
static int line_valid(const struct kosdec_adx_joint *j, enum kosdec_adx_joint_line line) {
    return (unsigned)line < (j->kind == KOSDEC_ADX_JOINT_QUEUE ? 4u : 2u);
}
static enum kosdec_adx_joint_status fail(struct kosdec_adx_joint *j,
                                        enum kosdec_adx_joint_status status) {
    if(valid(j) && j->error) j->error(j->error_context, status);
    return status;
}
static size_t minimum(size_t a, size_t b) { return a < b ? a : b; }
static int busy(const struct kosdec_adx_joint *j) {
    for(unsigned line = 0; line < 4; ++line)
        if(j->borrowed[line].bytes) return 1;
    return 0;
}
static void reset(struct kosdec_adx_joint *j) {
    j->sealed = false;
    if(j->kind == KOSDEC_ADX_JOINT_QUEUE) {
        for(size_t i = 0; i < j->node_count; ++i)
            j->nodes[i] = (struct kosdec_adx_joint_node){
                .next = i + 1 < j->node_count ? &j->nodes[i + 1] : NULL};
        j->spare = j->nodes; j->spare_count = j->node_count; j->capacity = 0;
        for(unsigned line = 0; line < 4; ++line) {
            j->head[line] = j->tail[line] = j->reserved[line] = NULL;
            j->chunks[line] = j->available[line] = 0;
        }
        return;
    }
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
enum kosdec_adx_joint_status kosdec_adx_joint_queue_init(
    struct kosdec_adx_joint *j, struct kosdec_adx_joint_node *nodes,
    size_t count, enum kosdec_adx_joint_mode mode) {
    if(!j || !nodes || !count || count > INT32_MAX / sizeof(*nodes) ||
       (mode != KOSDEC_ADX_JOINT_SEPARATE && mode != KOSDEC_ADX_JOINT_JOIN))
        return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    *j = (struct kosdec_adx_joint){0};
    j->magic = JOINT_MAGIC; j->kind = KOSDEC_ADX_JOINT_QUEUE;
    j->nodes = nodes; j->node_count = count; j->mode = mode;
    reset(j);
    return KOSDEC_ADX_JOINT_OK;
}
static struct kosdec_adx_joint_node *take_node(struct kosdec_adx_joint *j) {
    struct kosdec_adx_joint_node *node = j->spare;
    if(node) { j->spare = node->next; --j->spare_count; }
    return node;
}
static void free_node(struct kosdec_adx_joint *j, struct kosdec_adx_joint_node *node) {
    node->next = j->spare; j->spare = node; ++j->spare_count;
}
/* Returns -1 without mutation on exhaustion; 1 if the reserved node was used. */
static int enqueue(struct kosdec_adx_joint *j, unsigned line, uint8_t *data,
    size_t bytes, int prepend, struct kosdec_adx_joint_node *reserved) {
    struct kosdec_adx_joint_node *edge = prepend ? j->head[line] : j->tail[line];
    if(j->mode == KOSDEC_ADX_JOINT_JOIN && edge &&
       (prepend ? data + bytes == edge->data : edge->data + edge->bytes == data)) {
        if(prepend) edge->data = data;
        edge->bytes += bytes;
        return 0;
    }
    struct kosdec_adx_joint_node *node = reserved ? reserved : take_node(j);
    if(!node) return -1;
    *node = (struct kosdec_adx_joint_node){.data=data, .bytes=bytes};
    if(prepend) { node->next = j->head[line]; j->head[line] = node; }
    else {
        if(j->tail[line]) j->tail[line]->next = node;
        else j->head[line] = node;
    }
    if(!prepend || !j->tail[line]) j->tail[line] = node;
    ++j->chunks[line];
    return reserved != NULL;
}
static int overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn) {
    return a < b ? b - a < an : a - b < bn;
}
enum kosdec_adx_joint_status kosdec_adx_joint_seed(struct kosdec_adx_joint *j,
    enum kosdec_adx_joint_line line, void *data, size_t bytes) {
    if(!valid(j) || j->kind != KOSDEC_ADX_JOINT_QUEUE || !line_valid(j, line) ||
       (bytes && !data)) return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    if(!bytes) return KOSDEC_ADX_JOINT_OK;
    if(j->sealed) return fail(j, KOSDEC_ADX_JOINT_SEALED);
    uintptr_t address = (uintptr_t)data;
    if(bytes > (size_t)INT32_MAX - j->capacity || bytes > UINTPTR_MAX - address)
        return fail(j, KOSDEC_ADX_JOINT_LIMIT);
    if(overlap(address, bytes, (uintptr_t)j, sizeof(*j)) ||
       overlap(address, bytes, (uintptr_t)j->nodes, j->node_count * sizeof(*j->nodes)))
        return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    for(unsigned l = 0; l < 4; ++l) {
        if(j->borrowed[l].bytes && overlap(address, bytes,
            (uintptr_t)j->borrowed[l].data, j->borrowed[l].bytes))
            return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
        for(struct kosdec_adx_joint_node *n = j->head[l]; n; n = n->next)
            if(overlap(address, bytes, (uintptr_t)n->data, n->bytes))
                return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    }
    if(enqueue(j, line, data, bytes, 0, NULL) < 0) return fail(j, KOSDEC_ADX_JOINT_LIMIT);
    j->available[line] += bytes; j->capacity += bytes;
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_reset(struct kosdec_adx_joint *j) {
    if(!valid(j)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    if(busy(j)) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    reset(j);
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_finish(struct kosdec_adx_joint *j) {
    if(!valid(j)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    for(unsigned line = 0; line < 4; ++line)
        if(line != KOSDEC_ADX_JOINT_DATA && j->borrowed[line].bytes)
            return fail(j, KOSDEC_ADX_JOINT_BUSY);
    j->sealed = true;
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
    if(j->kind == KOSDEC_ADX_JOINT_QUEUE)
        return j->head[line] ? j->head[line]->bytes : 0;
    return minimum(j->available[line], j->capacity - j->position[line] + j->extra);
}
enum kosdec_adx_joint_status kosdec_adx_joint_query(
    struct kosdec_adx_joint *j, struct kosdec_adx_joint_info *info) {
    if(!valid(j) || !info) return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    *info = (struct kosdec_adx_joint_info){.kind=j->kind,
        .capacity=j->capacity, .extra=j->extra, .mode=j->mode,
        .spare_nodes=j->spare_count, .total_nodes=j->node_count, .sealed=j->sealed};
    for(unsigned line = 0; line < 4; ++line) {
        info->available[line] = j->available[line];
        info->contiguous[line] = contiguous(j, line);
        info->borrowed[line] = j->borrowed[line].bytes;
        info->chunks[line] = j->chunks[line];
    }
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_get(struct kosdec_adx_joint *j,
    enum kosdec_adx_joint_line line, size_t request, struct kosdec_adx_chunk *out) {
    if(!valid(j) || !line_valid(j, line) || !out)
        return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    if(request && j->sealed && line != KOSDEC_ADX_JOINT_DATA)
        return fail(j, KOSDEC_ADX_JOINT_SEALED);
    if(request && j->borrowed[line].bytes) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    size_t n = minimum(request, contiguous(j, line));
    if(j->kind == KOSDEC_ADX_JOINT_QUEUE && j->mode == KOSDEC_ADX_JOINT_SEPARATE &&
       n < contiguous(j, line)) n = 0;
    if(n && j->next_token == UINT64_MAX) return fail(j, KOSDEC_ADX_JOINT_LIMIT);
    struct kosdec_adx_chunk chunk = {0};
    if(n && j->kind == KOSDEC_ADX_JOINT_QUEUE) {
        struct kosdec_adx_joint_node *head = j->head[line], *reserved;
        if(n < head->bytes) {
            reserved = take_node(j);
            if(!reserved) return fail(j, KOSDEC_ADX_JOINT_LIMIT);
        } else {
            reserved = head; j->head[line] = head->next;
            if(!j->head[line]) j->tail[line] = NULL;
            --j->chunks[line];
        }
        chunk = (struct kosdec_adx_chunk){head->data, n, j, ++j->next_token, line};
        if(reserved != head) { head->data += n; head->bytes -= n; }
        j->reserved[line] = reserved; j->borrowed[line] = chunk;
        j->available[line] -= n;
        *out = chunk;
        return KOSDEC_ADX_JOINT_OK;
    }
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
    if(c->owner != j || !line_valid(j, c->line)) return fail(j, KOSDEC_ADX_JOINT_STALE);
    const struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    if(!b->bytes || b->token != c->token || c->bytes > b->bytes ||
       c->data != b->data + (suffix ? b->bytes - c->bytes : 0))
        return fail(j, KOSDEC_ADX_JOINT_STALE);
    return KOSDEC_ADX_JOINT_OK;
}
static enum kosdec_adx_joint_status return_queue(struct kosdec_adx_joint *j,
    unsigned destination, const struct kosdec_adx_chunk *c, int prepend) {
    if(j->sealed && !prepend && destination == KOSDEC_ADX_JOINT_DATA)
        return fail(j, KOSDEC_ADX_JOINT_SEALED);
    struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    int whole = c->bytes == b->bytes;
    int used = enqueue(j, destination, c->data, c->bytes, prepend,
                       whole ? j->reserved[c->line] : NULL);
    if(used < 0) return fail(j, KOSDEC_ADX_JOINT_LIMIT);
    j->available[destination] += c->bytes;
    if(!prepend) b->data += c->bytes;
    b->bytes -= c->bytes;
    if(whole) {
        if(!used) free_node(j, j->reserved[c->line]);
        j->reserved[c->line] = NULL;
        *b = (struct kosdec_adx_chunk){0};
    }
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_move(struct kosdec_adx_joint *j,
    enum kosdec_adx_joint_line destination, const struct kosdec_adx_chunk *c) {
    if(!valid(j) || j->kind != KOSDEC_ADX_JOINT_QUEUE || !line_valid(j, destination))
        return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    enum kosdec_adx_joint_status status = check(j, c, 0);
    if(status != KOSDEC_ADX_JOINT_OK || !c->bytes) return status;
    return return_queue(j, destination, c, 0);
}
enum kosdec_adx_joint_status kosdec_adx_joint_detach(
    struct kosdec_adx_joint *j, const struct kosdec_adx_chunk *c) {
    if(!valid(j) || j->kind != KOSDEC_ADX_JOINT_QUEUE)
        return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    enum kosdec_adx_joint_status status = check(j, c, 0);
    if(status != KOSDEC_ADX_JOINT_OK || !c->bytes) return status;
    struct kosdec_adx_chunk *b = &j->borrowed[c->line];
    b->data += c->bytes; b->bytes -= c->bytes; j->capacity -= c->bytes;
    if(!b->bytes) {
        free_node(j, j->reserved[c->line]); j->reserved[c->line] = NULL;
        *b = (struct kosdec_adx_chunk){0};
    }
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
enum kosdec_adx_joint_status kosdec_adx_joint_insert_silence(
    struct kosdec_adx_joint *j, unsigned channels, uint32_t requested, uint32_t *inserted) {
    if(!valid(j) || j->kind != KOSDEC_ADX_JOINT_RING || !inserted ||
       (channels != 1 && channels != 2)) return fail(j, KOSDEC_ADX_JOINT_BAD_ARGUMENT);
    if(j->sealed) return fail(j, KOSDEC_ADX_JOINT_SEALED);
    if(j->borrowed[KOSDEC_ADX_JOINT_FREE].bytes) return fail(j, KOSDEC_ADX_JOINT_BUSY);
    size_t group = 18u * channels;
    size_t groups = minimum(requested / 32u, 512u / group);
    groups = minimum(groups, j->available[KOSDEC_ADX_JOINT_FREE] / group);
    size_t bytes = groups * group;
    if(bytes) {
        size_t at = j->position[KOSDEC_ADX_JOINT_FREE];
        size_t first = minimum(bytes, j->capacity - at);
        struct kosdec_adx_chunk span = {.data = j->buffer + at, .bytes = first};
        memset(span.data, 0, span.bytes);
        publish(j, &span);
        if(first < bytes) {
            span.data = j->buffer; span.bytes = bytes - first;
            memset(span.data, 0, span.bytes);
            publish(j, &span);
        }
        /* All admission is complete before writing. Publish accounting only
           after both spans; no partial second borrow can fail at the wrap. */
        at += bytes;
        if(at >= j->capacity) at -= j->capacity;
        j->position[KOSDEC_ADX_JOINT_FREE] = at;
        j->available[KOSDEC_ADX_JOINT_FREE] -= bytes;
        j->available[KOSDEC_ADX_JOINT_DATA] += bytes;
    }
    *inserted = (uint32_t)groups * 32u;
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_joint_status kosdec_adx_joint_put(
    struct kosdec_adx_joint *j, const struct kosdec_adx_chunk *c) {
    enum kosdec_adx_joint_status status = check(j, c, 0);
    if(status != KOSDEC_ADX_JOINT_OK || !c->bytes) return status;
    if(j->kind == KOSDEC_ADX_JOINT_QUEUE) return return_queue(j, c->line ^ 1u, c, 0);
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
    if(j->kind == KOSDEC_ADX_JOINT_QUEUE) return return_queue(j, c->line, c, 1);
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
