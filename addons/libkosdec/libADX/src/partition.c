#include <kosdec/adx_partition.h>
#include <string.h>
#define PARTITION_MAGIC UINT32_C(0x41445854)
#define REGISTRY_MAGIC UINT32_C(0x41445852)
#define OVERLAY_MAGIC UINT32_C(0x4144584f)
static bool valid(const struct kosdec_adx_partition *p) {
    return p && p->magic == PARTITION_MAGIC;
}
static bool registry_valid(const struct kosdec_adx_partition_registry *r) {
    return r && r->magic == REGISTRY_MAGIC;
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
size_t kosdec_adx_partition_work_size(uint32_t count) {
    return count <= KOSDEC_ADX_PARTITION_FILES ? count * sizeof(struct kosdec_adx_range) : 0;
}
enum kosdec_adx_status kosdec_adx_partition_init(struct kosdec_adx_partition *p,
    kosdec_adx_read_at read_at, void *context, uint32_t bytes,
    struct kosdec_adx_range *entries, size_t capacity) {
    if(!p || !read_at || capacity > KOSDEC_ADX_PARTITION_FILES ||
       (!entries && capacity) ||
       (entries && (uintptr_t)entries % _Alignof(struct kosdec_adx_range)))
        return KOSDEC_ADX_BAD_ARGUMENT;
    memset(p, 0, sizeof(*p));
    p->magic = PARTITION_MAGIC; p->input_bytes = bytes;
    p->read_at = read_at; p->context = context;
    p->entries = entries; p->capacity = capacity;
    if(bytes < 8) p->terminal = KOSDEC_ADX_TRUNCATED;
    return p->terminal;
}
void kosdec_adx_partition_cancel(struct kosdec_adx_partition *p) {
    if(valid(p) && !p->terminal) p->terminal = KOSDEC_ADX_CANCELLED;
}
enum kosdec_adx_status kosdec_adx_partition_step(struct kosdec_adx_partition *p) {
    if(!valid(p)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(p->terminal) return p->terminal;
    unsigned target = 8;
    uint32_t position = 0;
    if(p->header_ready) {
        uint32_t remaining = p->count - p->loaded;
        target = remaining < 64 ? remaining * 8 : 512;
        position = 8 + p->loaded * 8;
    }
    unsigned request = target - p->used;
    struct kosdec_adx_input_result in = p->read_at(p->context, position + p->used,
                                                 p->scratch + p->used, request);
    if(in.status == KOSDEC_ADX_INPUT_WAIT && !in.bytes) return KOSDEC_ADX_NEED_INPUT;
    if(in.status != KOSDEC_ADX_INPUT_DATA || !in.bytes || in.bytes > request) {
        p->terminal = in.status == KOSDEC_ADX_INPUT_END && !in.bytes ?
            KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
        return p->terminal;
    }
    p->used += (unsigned)in.bytes;
    if(p->used < target) return KOSDEC_ADX_MORE;
    p->used = 0;
    if(!p->header_ready) {
        if(memcmp(p->scratch, "AFS", 3)) return p->terminal = KOSDEC_ADX_INVALID;
        if(p->scratch[3]) return p->terminal = KOSDEC_ADX_UNSUPPORTED;
        uint32_t count = word(p->scratch + 4);
        if(count > KOSDEC_ADX_PARTITION_FILES) return p->terminal = KOSDEC_ADX_UNSUPPORTED;
        if(count > (p->input_bytes - 8u) / 8u) return p->terminal = KOSDEC_ADX_INVALID;
        p->count = count; p->header_ready = true;
        if(count > p->capacity) p->terminal = KOSDEC_ADX_NEED_OUTPUT;
        else if(!count) p->terminal = KOSDEC_ADX_DONE;
        return p->terminal;
    }
    uint32_t table_end = 8 + p->count * 8;
    for(unsigned i = 0; i < target; i += 8) {
        uint32_t offset = word(p->scratch + i), bytes = word(p->scratch + i + 4);
        if(offset > p->input_bytes || bytes > p->input_bytes - offset ||
           (bytes && offset < table_end)) return p->terminal = KOSDEC_ADX_INVALID;
        p->entries[p->loaded++] = (struct kosdec_adx_range){offset, bytes};
    }
    if(p->loaded == p->count) p->terminal = KOSDEC_ADX_DONE;
    return p->terminal;
}
bool kosdec_adx_partition_get_info(const struct kosdec_adx_partition *p,
                                 struct kosdec_adx_partition_info *out) {
    if(!valid(p) || !out) return false;
    *out = (struct kosdec_adx_partition_info){p->input_bytes, p->count, p->loaded,
        p->registrations, kosdec_adx_partition_work_size(p->count), p->header_ready, p->terminal};
    return true;
}
enum kosdec_adx_partition_status kosdec_adx_partition_get_range(
    const struct kosdec_adx_partition *p, uint32_t id, struct kosdec_adx_range *out) {
    if(!valid(p) || !out) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(p->terminal != KOSDEC_ADX_DONE) return KOSDEC_ADX_PARTITION_NOT_READY;
    if(id >= p->count) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    *out = p->entries[id];
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_open_view(
    const struct kosdec_adx_partition *p, uint32_t id, kosdec_adx_read_at read_at,
    void *context, struct kosdec_adx_view *view) {
    if(!view || !read_at) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    struct kosdec_adx_range range;
    enum kosdec_adx_partition_status status = kosdec_adx_partition_get_range(p, id, &range);
    if(status != KOSDEC_ADX_PARTITION_OK) return status;
    if(kosdec_adx_view_init(view, read_at, context, p->input_bytes, range.offset, range.bytes)
       != KOSDEC_ADX_DONE) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_close(struct kosdec_adx_partition *p) {
    if(!valid(p)) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(p->registrations) return KOSDEC_ADX_PARTITION_BUSY;
    memset(p, 0, sizeof(*p));
    return KOSDEC_ADX_PARTITION_OK;
}
void kosdec_adx_partition_registry_init(struct kosdec_adx_partition_registry *r) {
    if(r) { memset(r, 0, sizeof(*r)); r->magic = REGISTRY_MAGIC; }
}
static struct kosdec_adx_partition_overlay *detach(struct kosdec_adx_partition_registry *r, unsigned id) {
    struct kosdec_adx_partition_overlay *o = r->overlays[id];
    r->overlays[id] = o->previous;
    --o->partition->registrations;
    o->owner = NULL; o->partition = NULL; o->previous = NULL; o->id = 0;
    return o;
}
static void detach_all(struct kosdec_adx_partition_registry *r, unsigned id) {
    while(r->overlays[id]) (void)detach(r, id);
}
enum kosdec_adx_partition_status kosdec_adx_partition_bind(
    struct kosdec_adx_partition_registry *r, unsigned id, struct kosdec_adx_partition *p) {
    if(!registry_valid(r) || id >= KOSDEC_ADX_PARTITION_IDS || !valid(p))
        return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(p->terminal != KOSDEC_ADX_DONE) return KOSDEC_ADX_PARTITION_NOT_READY;
    if(r->slots[id] == p) return KOSDEC_ADX_PARTITION_OK;
    if(p->registrations == UINT32_MAX) return KOSDEC_ADX_PARTITION_LIMIT;
    detach_all(r, id);
    if(r->slots[id]) --r->slots[id]->registrations;
    r->slots[id] = p; ++p->registrations;
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_unbind(
    struct kosdec_adx_partition_registry *r, unsigned id) {
    if(!registry_valid(r) || id >= KOSDEC_ADX_PARTITION_IDS) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(!r->slots[id]) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    detach_all(r, id);
    --r->slots[id]->registrations; r->slots[id] = NULL;
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_lookup(
    const struct kosdec_adx_partition_registry *r, unsigned id, const struct kosdec_adx_partition **out) {
    if(!registry_valid(r) || id >= KOSDEC_ADX_PARTITION_IDS || !out) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(!r->slots[id]) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    *out = r->overlays[id] ? r->overlays[id]->partition : r->slots[id];
    return KOSDEC_ADX_PARTITION_OK;
}
void kosdec_adx_partition_overlay_init(struct kosdec_adx_partition_overlay *o) {
    if(o) { memset(o, 0, sizeof(*o)); o->magic = OVERLAY_MAGIC; }
}
enum kosdec_adx_partition_status kosdec_adx_partition_push(
    struct kosdec_adx_partition_registry *r, unsigned id,
    struct kosdec_adx_partition_overlay *o, struct kosdec_adx_partition *p) {
    if(!registry_valid(r) || id >= KOSDEC_ADX_PARTITION_IDS || !o ||
       o->magic != OVERLAY_MAGIC || !valid(p)) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(o->owner) return KOSDEC_ADX_PARTITION_BUSY;
    if(!r->slots[id]) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    if(p->terminal != KOSDEC_ADX_DONE) return KOSDEC_ADX_PARTITION_NOT_READY;
    if(p->registrations == UINT32_MAX) return KOSDEC_ADX_PARTITION_LIMIT;
    o->owner = r; o->id = id; o->partition = p; o->previous = r->overlays[id];
    r->overlays[id] = o; ++p->registrations;
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_pop(
    struct kosdec_adx_partition_registry *r, unsigned id, struct kosdec_adx_partition_overlay **out) {
    if(!registry_valid(r) || id >= KOSDEC_ADX_PARTITION_IDS) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    if(!r->overlays[id]) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    struct kosdec_adx_partition_overlay *o = detach(r, id);
    if(out) *out = o;
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_resolve(
    const struct kosdec_adx_partition_registry *r, unsigned id, uint32_t file,
    struct kosdec_adx_partition_resolution *out) {
    if(!out) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    const struct kosdec_adx_partition *p;
    enum kosdec_adx_partition_status status = kosdec_adx_partition_lookup(r, id, &p);
    if(status != KOSDEC_ADX_PARTITION_OK) return status;
    if(file >= p->count) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    for(const struct kosdec_adx_partition_overlay *o = r->overlays[id]; o; o = o->previous) {
        p = o->partition;
        if(file < p->count && p->entries[file].offset) {
            *out = (struct kosdec_adx_partition_resolution){p, p->entries[file]};
            return KOSDEC_ADX_PARTITION_OK;
        }
    }
    p = r->slots[id];
    if(file >= p->count) return KOSDEC_ADX_PARTITION_NOT_FOUND;
    *out = (struct kosdec_adx_partition_resolution){p, p->entries[file]};
    return KOSDEC_ADX_PARTITION_OK;
}
enum kosdec_adx_partition_status kosdec_adx_partition_registry_close(struct kosdec_adx_partition_registry *r) {
    if(!registry_valid(r)) return KOSDEC_ADX_PARTITION_BAD_ARGUMENT;
    for(unsigned i = 0; i < KOSDEC_ADX_PARTITION_IDS; ++i) {
        detach_all(r, i);
        if(r->slots[i]) --r->slots[i]->registrations;
    }
    memset(r, 0, sizeof(*r));
    return KOSDEC_ADX_PARTITION_OK;
}
