#include "adx_joint_reader_internal.h"
#include "adx_decoder_internal.h"
#include "adx_filter.h"
#include "adx_joint_internal.h"
#include "adx_math.h"
#include <kosdec/adx_loader.h>
#include <string.h>
#define READER_MAGIC UINT32_C(0x4144584b)
static int valid(const struct kosdec_adx_joint_reader *r) {
    return r && r->magic == READER_MAGIC;
}
bool media_adx_joint_reader_enable_live_sfa(struct kosdec_adx_joint_reader *r) {
    if(!valid(r) || r->terminal || r->consumed_bytes || r->held.bytes ||
       r->sfa_live || r->sector.request.read_at || r->linked.enabled)
        return false;
    r->sfa_live = true;
    return true;
}
static enum kosdec_adx_status check_sfa_header(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_decoder *d = &r->decoder;
    if(d->header_bytes != KOSDEC_ADX_SFA_HEADER_BYTES || d->version != 3 ||
       d->cutoff != 500 || d->loop.present || d->total_frames != KOSDEC_ADX_MAX_FRAMES)
        return KOSDEC_ADX_INVALID;
    if(r->sfa_length_known) d->total_frames = r->sfa_frames;
    return KOSDEC_ADX_MORE;
}
bool media_adx_joint_reader_set_sfa_length(struct kosdec_adx_joint_reader *r,
    struct kosdec_adx_joint *joint, uint32_t frames) {
    if(!valid(r) || r->joint != joint || !r->sfa_live || r->sfa_length_known ||
       r->terminal || r->decoder.frames_done > frames) return false;
    r->sfa_length_known = true;
    r->sfa_frames = frames;
    if(r->decoder.header_ready) r->decoder.total_frames = frames;
    return true;
}
enum kosdec_adx_joint_status kosdec_adx_joint_reader_init(
    struct kosdec_adx_joint_reader *r, struct kosdec_adx_joint *j) {
    if(!r) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    struct kosdec_adx_joint_info info;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_query(j, &info);
    if(s != KOSDEC_ADX_JOINT_OK) return s;
    if(info.borrowed[KOSDEC_ADX_JOINT_DATA]) return KOSDEC_ADX_JOINT_BUSY;
    *r = (struct kosdec_adx_joint_reader){0};
    r->magic = READER_MAGIC; r->joint = j; kosdec_adx_init(&r->decoder);
    return KOSDEC_ADX_JOINT_OK;
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_filter(
    struct kosdec_adx_joint_reader *r, kosdec_adx_pcm_filter fn, void *context) {
    if(!valid(r)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->terminal) return r->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : r->terminal;
    return kosdec_adx_set_filter(&r->decoder, fn, context);
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_feed(
    struct kosdec_adx_joint_reader *r, kosdec_adx_joint_feed fn, void *context) {
    if(!valid(r) || r->sector.request.read_at || r->linked.loader)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->terminal) return r->terminal == KOSDEC_ADX_DONE ? KOSDEC_ADX_BAD_ARGUMENT : r->terminal;
    r->feed = fn; r->feed_context = fn ? context : NULL;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_enable_linked(struct kosdec_adx_joint_reader *r) {
    if(!valid(r) || r->terminal || r->consumed_bytes || r->held.bytes || r->linked.enabled)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->sfa_live || r->sector.request.read_at || r->job.repeat)
        return KOSDEC_ADX_UNSUPPORTED;
    r->linked.enabled = r->linked.follow = true;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_enable_link_scan(struct kosdec_adx_joint_reader *r) {
    if(!valid(r) || !r->linked.enabled || r->terminal || r->consumed_bytes || r->held.bytes)
        return KOSDEC_ADX_BAD_ARGUMENT;
    r->linked.scan = true;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_loader(struct kosdec_adx_joint_reader *r,
    struct kosdec_adx_loader *loader, bool seal_when_empty) {
    struct kosdec_adx_joint_info info;
    if(!valid(r) || !r->linked.enabled || r->terminal || r->consumed_bytes ||
       r->held.bytes || r->feed || r->linked.loader ||
       kosdec_adx_loader_get_input_joint(loader) != r->joint ||
       kosdec_adx_joint_query(r->joint, &info) != KOSDEC_ADX_JOINT_OK || info.sealed)
        return KOSDEC_ADX_BAD_ARGUMENT;
    r->linked.loader = loader;
    r->linked.seal_when_empty = seal_when_empty;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_link_follow(
    struct kosdec_adx_joint_reader *r, bool enabled) {
    if(!valid(r)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(!r->linked.enabled) return KOSDEC_ADX_UNSUPPORTED;
    if(r->terminal) return r->terminal;
    if(enabled && r->linked.released) return KOSDEC_ADX_BAD_ARGUMENT;
    r->linked.follow = enabled;
    return KOSDEC_ADX_MORE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_release_link(struct kosdec_adx_joint_reader *r) {
    enum kosdec_adx_status status = kosdec_adx_joint_reader_set_link_follow(r, false);
    if(status == KOSDEC_ADX_MORE) r->linked.released = true;
    return status;
}
enum kosdec_adx_status kosdec_adx_joint_reader_enable_jobs(
    struct kosdec_adx_joint_reader *r, int16_t *pcm, size_t samples,
    unsigned maximum, unsigned extra, bool repeat) {
    if(!valid(r) || r->terminal || r->consumed_bytes || r->held.bytes ||
       r->job.pcm || !pcm || (uintptr_t)pcm % _Alignof(int16_t) ||
       maximum < 32 || maximum > MEDIA_ADX_JOB_MAX_FRAMES ||
       samples < 2u * maximum || extra > 31)
        return KOSDEC_ADX_BAD_ARGUMENT;
    struct kosdec_adx_joint_info info;
    if(kosdec_adx_joint_query(r->joint, &info) != KOSDEC_ADX_JOINT_OK ||
       info.borrowed[KOSDEC_ADX_JOINT_DATA])
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->linked.enabled && repeat) return KOSDEC_ADX_UNSUPPORTED;
    switch(info.kind) {
    case KOSDEC_ADX_JOINT_MEMORY:
        if(info.available[KOSDEC_ADX_JOINT_DATA] != info.capacity)
            return KOSDEC_ADX_BAD_ARGUMENT;
        break;
    case KOSDEC_ADX_JOINT_RING:
        if(repeat || info.extra < 35)
            return KOSDEC_ADX_UNSUPPORTED;
        break;
    case KOSDEC_ADX_JOINT_QUEUE:
        if(repeat) return KOSDEC_ADX_UNSUPPORTED;
        break;
    default:
        return KOSDEC_ADX_UNSUPPORTED;
    }
    r->job.pcm = pcm;
    r->job.maximum = maximum;
    r->job.extra = extra;
    r->job.repeat = repeat;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_set_sector_source(
    struct kosdec_adx_joint_reader *r, kosdec_adx_read_at read_at, void *context,
    uint32_t bytes, uint32_t count, bool repeat) {
    const unsigned sector = KOSDEC_ADX_REQUEST_SECTOR_BYTES;
    struct kosdec_adx_joint_info info;
    if(!valid(r) || r->terminal || !r->job.pcm || r->consumed_bytes ||
       r->held.bytes || r->feed || r->sfa_live || r->linked.enabled ||
       r->sector.request.read_at || !read_at ||
       !bytes || !count || kosdec_adx_joint_query(r->joint, &info) != KOSDEC_ADX_JOINT_OK ||
       info.kind != KOSDEC_ADX_JOINT_RING || info.sealed ||
       info.available[KOSDEC_ADX_JOINT_FREE] != info.capacity ||
       r->joint->position[KOSDEC_ADX_JOINT_FREE] % sector ||
       info.capacity % sector || info.capacity < 2u * sector ||
       count > info.capacity / sector)
        return KOSDEC_ADX_BAD_ARGUMENT;
    if(kosdec_adx_request_init(&r->sector.request, read_at, context, bytes) !=
       KOSDEC_ADX_REQUEST_STOPPED)
        return KOSDEC_ADX_BAD_ARGUMENT;
    r->sector.sectors_per_read = count;
    r->job.repeat = repeat;
    return KOSDEC_ADX_DONE;
}
enum kosdec_adx_status kosdec_adx_joint_reader_release_loop(
    struct kosdec_adx_joint_reader *r) {
    if(!valid(r)) return KOSDEC_ADX_BAD_ARGUMENT;
    if(r->terminal) return r->terminal;
    if(!r->job.pcm || (r->joint->kind != KOSDEC_ADX_JOINT_MEMORY &&
                      !r->sector.request.read_at))
        return KOSDEC_ADX_UNSUPPORTED;
    r->job.repeat = false;
    return KOSDEC_ADX_MORE;
}
static enum kosdec_adx_joint_status release(struct kosdec_adx_joint_reader *r) {
    if(!r->held.bytes) return KOSDEC_ADX_JOINT_OK;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_put(r->joint, &r->held);
    if(s == KOSDEC_ADX_JOINT_OK) {
        r->held = (struct kosdec_adx_chunk){0}; r->used = 0;
    }
    return s;
}
static enum kosdec_adx_joint_status retire_job_input(
    struct kosdec_adx_joint_reader *r, size_t consumed) {
    struct kosdec_adx_chunk prefix, suffix;
    kosdec_adx_chunk_split(&r->held, consumed, &prefix, &suffix);
    if(r->joint->kind == KOSDEC_ADX_JOINT_QUEUE) {
        /* Use the reserved node for the final whole return. If a partial
           put cannot fit the pool, the original complete loan is intact. */
        enum kosdec_adx_joint_status status = kosdec_adx_joint_put(r->joint, &prefix);
        if(status != KOSDEC_ADX_JOINT_OK) return status;
        r->held = suffix;
        status = kosdec_adx_joint_unget(r->joint, &r->held);
        if(status == KOSDEC_ADX_JOINT_OK) r->held = (struct kosdec_adx_chunk){0};
        return status;
    }
    enum kosdec_adx_joint_status status = kosdec_adx_joint_unget(r->joint, &suffix);
    if(status != KOSDEC_ADX_JOINT_OK) return status;
    r->held = prefix;
    return release(r);
}
static enum kosdec_adx_joint_status unget_link_next(struct kosdec_adx_joint_reader *r) {
    if(!r->linked.next.bytes) return KOSDEC_ADX_JOINT_OK;
    enum kosdec_adx_joint_status s = kosdec_adx_joint_unget(r->joint, &r->linked.next);
    if(s == KOSDEC_ADX_JOINT_OK) r->linked.next = (struct kosdec_adx_chunk){0};
    return s;
}
static enum kosdec_adx_joint_status begin_link_scan(struct kosdec_adx_joint_reader *r) {
    enum kosdec_adx_joint_status s = retire_job_input(r, r->used);
    if(s != KOSDEC_ADX_JOINT_OK) return s;
    r->used = 0;
    s = kosdec_adx_joint_get(r->joint, KOSDEC_ADX_JOINT_DATA, SIZE_MAX, &r->held);
    if(s != KOSDEC_ADX_JOINT_OK || !r->held.bytes) return s;
    return kosdec_adx_joint_get_next_data(r->joint, SIZE_MAX, &r->linked.next);
}
static bool buffered_header(const struct kosdec_adx_chunk *c, size_t at,
                            const struct kosdec_adx_chunk *next) {
    size_t contiguous = c->bytes - at;
    size_t total = contiguous + (next ? next->bytes : 0);
    if(total < 4) return false;
    uint8_t high = contiguous > 2 ? c->data[at + 2] : next->data[2 - contiguous];
    uint8_t low = contiguous > 3 ? c->data[at + 3] : next->data[3 - contiguous];
    return ((unsigned)high << 8) + low + 4u <= total;
}
static struct kosdec_adx_joint_read_result scan_link(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_joint_read_result result = {KOSDEC_ADX_MORE, KOSDEC_ADX_JOINT_OK, 0};
    unsigned budget = KOSDEC_ADX_HEADER_BUDGET;
    while(r->linked.follow && budget) {
        bool second = r->linked.scan_second;
        const struct kosdec_adx_chunk *c = second ? &r->linked.next : &r->held;
        size_t at = r->linked.scan_offset;
        if(c->bytes - at < 2) {
            if(second) break;
            r->linked.scan_second = true;
            r->linked.scan_offset = 0;
            continue;
        }
        if(c->data[at] == 0x80 && c->data[at + 1] == 0) {
            if(!buffered_header(c, at, second ? NULL : &r->linked.next)) break;
            size_t skipped = at;
            if(second) {
                skipped += r->held.bytes;
                result.joint_status = release(r);
                if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
                r->held = r->linked.next;
                r->linked.next = (struct kosdec_adx_chunk){0};
            }
            else {
                result.joint_status = unget_link_next(r);
                if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
            }
            result.joint_status = retire_job_input(r, at);
            if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
            r->consumed_bytes += skipped;
            r->linked.scan_active = false;
            r->linked.header_selected = true;
            return result;
        }
        r->linked.scan_offset += 2;
        budget -= 2;
    }
    if(!budget) return result;
    result.joint_status = unget_link_next(r);
    if(result.joint_status == KOSDEC_ADX_JOINT_OK)
        result.joint_status = retire_job_input(r, 0);
    if(result.joint_status == KOSDEC_ADX_JOINT_OK) {
        r->linked.scan_active = r->linked.follow = false;
        result.status = KOSDEC_ADX_DONE;
    }
    return result;
}
static enum kosdec_adx_joint_status discard_sector_padding(
    struct kosdec_adx_joint_reader *r) {
    enum kosdec_adx_joint_status status = kosdec_adx_joint_get(r->joint,
        KOSDEC_ADX_JOINT_DATA, r->sector.padding, &r->held);
    if(status != KOSDEC_ADX_JOINT_OK) return status;
    /* A complete prefetched sector supplies all of its residual bytes.
       Leave a short loan readable and fail without publishing this job. */
    if(r->held.bytes != r->sector.padding) {
        status = retire_job_input(r, 0);
        return status == KOSDEC_ADX_JOINT_OK ? KOSDEC_ADX_JOINT_LIMIT : status;
    }
    return release(r);
}
static bool end_marker(const uint8_t *data, size_t bytes) {
    return bytes >= 2 && data[0] == 0x80 && data[1] == 1;
}
static enum kosdec_adx_status admit_linked_header(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_decoder *d = &r->decoder;
    if(d->loop.present || (r->linked.channels &&
       (r->linked.channels != d->channels || r->linked.sample_rate != d->sample_rate))) {
        d->header_ready = false;
        return KOSDEC_ADX_UNSUPPORTED;
    }
    r->linked.channels = d->channels;
    r->linked.sample_rate = d->sample_rate;
    r->linked.header_selected = false;
    return KOSDEC_ADX_MORE;
}
static struct kosdec_adx_result read_footer(struct kosdec_adx_joint_reader *r,
    const uint8_t *input, size_t bytes, bool final_input) {
    struct kosdec_adx_decoder *d = &r->decoder;
    size_t take = bytes;
    if(r->linked.enabled && take > d->footer_bytes - d->footer_used)
        take = d->footer_bytes - d->footer_used;
    struct kosdec_adx_result result = kosdec_adx_decode(d, input, take, NULL, 0,
                                                       final_input && take == bytes);
    if(result.status == KOSDEC_ADX_NEED_INPUT && take < bytes)
        result.status = KOSDEC_ADX_MORE;
    return result;
}
static enum kosdec_adx_status complete_linked_record(struct kosdec_adx_joint_reader *r,
                                                    enum kosdec_adx_status status) {
    if(status != KOSDEC_ADX_NEED_END && status != KOSDEC_ADX_DONE) return status;
    ++r->linked.completed_records;
    if(!r->linked.follow || status == KOSDEC_ADX_DONE) return KOSDEC_ADX_DONE;
    kosdec_adx_pcm_filter filter = r->decoder.filter;
    void *context = r->decoder.filter_context;
    kosdec_adx_init(&r->decoder);
    (void)kosdec_adx_set_filter(&r->decoder, filter, context);
    if(r->linked.scan) {
        r->linked.scan_active = true;
        r->linked.scan_second = false;
        r->linked.scan_offset = 0;
    }
    return KOSDEC_ADX_MORE;
}
static struct kosdec_adx_result decode_job(struct kosdec_adx_joint_reader *r,
    size_t room, size_t tail, bool final_input, int16_t history[2][2],
    uint32_t *clips) {
    struct kosdec_adx_decoder *d = &r->decoder;
    const uint8_t *input = r->held.data;
    size_t bytes = r->held.bytes;
    uint32_t remaining = d->total_frames - d->frames_done;
    if(end_marker(input, bytes))
        return (struct kosdec_adx_result){
            r->sfa_live || r->linked.enabled ? KOSDEC_ADX_INVALID : KOSDEC_ADX_DONE, 0, 0};
    unsigned group_bytes = 18u * d->channels;
    if(bytes < group_bytes)
        return (struct kosdec_adx_result){
            final_input ? KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_NEED_INPUT, 0, 0};

    unsigned slots = room < r->job.maximum ? (unsigned)room : r->job.maximum;
    if(tail && tail + r->job.extra < slots)
        slots = (unsigned)tail + r->job.extra;
    unsigned groups;
    unsigned target_groups = MEDIA_ADX_JOB_MAX_FRAMES / 32u;
    unsigned padding = 0;
    if(r->job.trap) {
        uint32_t trap_frames = d->loop.end_frame - d->frames_done;
        target_groups = (trap_frames + 31u) / 32u;
        padding = target_groups * 32u - trap_frames;
        if(slots > trap_frames)
            slots += padding;
    }
    groups = slots / 32u;
    if(tail && (tail + 31u) / 32u < groups)
        groups = (unsigned)((tail + 31u) / 32u);
    if(groups > target_groups) groups = target_groups;
    if((r->sfa_live || r->linked.enabled) && groups > (remaining + 31u) / 32u)
        groups = (remaining + 31u) / 32u;
    if(!groups) return (struct kosdec_adx_result){KOSDEC_ADX_NEED_OUTPUT, 0, 0};
    if(groups > bytes / group_bytes) groups = (unsigned)(bytes / group_bytes);

    /* A packed job reserves complete groups. A terminator at a group edge
       ends the admitted prefix. Reject malformed control bits in either
       channel before invoking the predictor. */
    for(unsigned i = 0; i < groups; ++i) {
        const uint8_t *group = input + i * group_bytes;
        if(end_marker(group, group_bytes)) { groups = i; break; }
        for(unsigned ch = 0; ch < d->channels; ++ch)
            if(group[ch * 18u] & 0x80u)
                return (struct kosdec_adx_result){KOSDEC_ADX_INVALID, 0, 0};
    }
    unsigned physical = groups * 32u;
    unsigned visible = physical;
    /* The stock publication threshold counts physical channel blocks. */
    if(groups * d->channels >= target_groups) visible -= padding;
    if(visible > remaining) visible = remaining;
    memcpy(history, d->history, sizeof(d->history));
    *clips = 0;
    for(unsigned ch = 0; ch < d->channels; ++ch) {
        int count = media_adx_channel_job(input + ch * 18u, physical, visible,
            d->coefficient, history[ch], r->job.pcm + ch, d->channels);
        if(count < 0) return (struct kosdec_adx_result){KOSDEC_ADX_INVALID, 0, 0};
        *clips += (unsigned)count;
    }
    return (struct kosdec_adx_result){KOSDEC_ADX_MORE, groups * group_bytes, visible};
}
static struct kosdec_adx_joint_read_result read_job(
    struct kosdec_adx_joint_reader *r, int16_t *output, size_t frames,
    size_t tail, const struct kosdec_adx_joint_info *info) {
    struct kosdec_adx_decoder *d = &r->decoder;
    struct kosdec_adx_joint_read_result result = {KOSDEC_ADX_MORE, KOSDEC_ADX_JOINT_OK, 0};
    bool memory = info->kind == KOSDEC_ADX_JOINT_MEMORY;
    bool queue = info->kind == KOSDEC_ADX_JOINT_QUEUE;
    bool header_pending = !d->header_ready;
    bool samples_complete = !header_pending && d->frames_done == d->total_frames;
    if(samples_complete && !r->sfa_live && !r->linked.enabled) {
        result.status = KOSDEC_ADX_DONE;
        return result;
    }
    size_t request = header_pending || samples_complete ? KOSDEC_ADX_HEADER_BUDGET :
        36u * ((r->job.maximum + 31u) / 32u);
    if(queue) request = info->contiguous[KOSDEC_ADX_JOINT_DATA];
    result.joint_status = kosdec_adx_joint_get(r->joint, KOSDEC_ADX_JOINT_DATA,
        request, &r->held);
    if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
    bool final_input = (memory || info->sealed) &&
        info->available[KOSDEC_ADX_JOINT_DATA] == r->held.bytes;
    int16_t history[2][2] = {{0}};
    uint32_t clips = 0;
    struct kosdec_adx_result decoded;
    if(header_pending) {
        enum media_adx_header_mode mode = memory ? MEDIA_ADX_HEADER_MEMORY_JOB :
            r->sector.request.read_at ? MEDIA_ADX_HEADER_SECTOR_JOB : MEDIA_ADX_HEADER_STRICT;
        decoded = media_adx_decode_header(d, r->held.data, r->held.bytes, final_input, mode);
    }
    else if(samples_complete)
        decoded = read_footer(r, r->held.data, r->held.bytes, final_input);
    else
        decoded = decode_job(r, frames, tail, final_input || (queue && info->sealed),
                             history, &clips);
    result.joint_status = retire_job_input(r, decoded.consumed);
    if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
    r->consumed_bytes += decoded.consumed;
    if(header_pending && r->sfa_live && d->header_ready)
        decoded.status = check_sfa_header(r);
    if(r->linked.enabled) {
        if(header_pending && d->header_ready)
            decoded.status = admit_linked_header(r);
        else if(samples_complete)
            decoded.status = complete_linked_record(r, decoded.status);
    }
    if(header_pending && (queue || r->sector.request.read_at) &&
       decoded.status == KOSDEC_ADX_NEED_INPUT &&
       decoded.consumed && info->available[KOSDEC_ADX_JOINT_DATA] > decoded.consumed)
        decoded.status = KOSDEC_ADX_MORE;
    if(header_pending && d->header_ready && memory && d->loop.present) {
        if(d->loop.end_byte > info->capacity ||
           18u * d->channels > info->capacity - d->loop.start_byte) {
            d->header_ready = false;
            decoded.status = KOSDEC_ADX_INVALID;
        }
        else r->job.trap = true;
    }
    if(decoded.frames) {
        uint32_t next = d->frames_done + (uint32_t)decoded.frames;
        if(r->job.trap && next == d->loop.end_frame) {
            if(r->sector.request.read_at) {
                /* Release changes the trap only at sector EOF. A prefetched
                   seek may precede the release even while this job is pending. */
                result.joint_status = discard_sector_padding(r);
                if(result.joint_status != KOSDEC_ADX_JOINT_OK) return result;
                next = d->loop.start_frame;
                ++r->job.wraps;
            }
            else if(r->job.repeat) {
                if(!media_adx_joint_rewind_memory(r->joint, d->loop.start_byte)) {
                    result.status = KOSDEC_ADX_IO_ERROR;
                    return result;
                }
                next = d->loop.start_frame;
                ++r->job.wraps;
            }
            else r->job.trap = false;
        }
        r->job.first_frame = d->frames_done;
        d->frames_done = next;
        memcpy(d->history, history, sizeof(history));
        d->clips += clips;
        for(size_t at = 0; d->filter && at < decoded.frames; at += 32) {
            size_t left = decoded.frames - at;
            media_adx_filter_pcm(d->filter, d->filter_context, d->channels,
                r->job.pcm + at * d->channels, left < 32 ? left : 32);
        }
        if(output != r->job.pcm)
            memcpy(output, r->job.pcm, decoded.frames * d->channels * sizeof(*output));
        result.frames = decoded.frames;
    }
    result.status = decoded.status;
    return result;
}
static enum kosdec_adx_joint_feed_status step_feed(struct kosdec_adx_joint_reader *r,
                                                   bool *retry_input) {
    struct kosdec_adx_loader *loader = r->linked.loader;
    if(!loader) return r->feed(r->feed_context, r->joint);
    enum kosdec_adx_loader_status status = kosdec_adx_loader_step(loader);
    if(status == KOSDEC_ADX_LOADER_NEED_INPUT) {
        *retry_input = true;
        return KOSDEC_ADX_JOINT_FEED_WAIT;
    }
    if(status == KOSDEC_ADX_LOADER_NEED_OUTPUT)
        return KOSDEC_ADX_JOINT_FEED_WAIT;
    struct kosdec_adx_loader_info info;
    if(status != KOSDEC_ADX_LOADER_OK || !kosdec_adx_loader_get_info(loader, &info))
        return KOSDEC_ADX_JOINT_FEED_ERROR;
    if(info.state == KOSDEC_ADX_LOADER_WAIT && r->linked.seal_when_empty)
        return KOSDEC_ADX_JOINT_FEED_END;
    return info.state == KOSDEC_ADX_LOADER_EXEC && !info.paused ?
        KOSDEC_ADX_JOINT_FEED_MORE : KOSDEC_ADX_JOINT_FEED_WAIT;
}
static enum kosdec_adx_status loader_error(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_loader_info info;
    return kosdec_adx_loader_get_info(r->linked.loader,&info) &&
           info.state == KOSDEC_ADX_LOADER_FAILED &&
           info.error == KOSDEC_ADX_REQUEST_TRUNCATED ?
        KOSDEC_ADX_TRUNCATED : KOSDEC_ADX_IO_ERROR;
}

enum kosdec_adx_status media_adx_joint_reader_step_loader(
    struct kosdec_adx_joint_reader *r, bool reload) {
    struct kosdec_adx_loader *loader = r->linked.loader;
    enum kosdec_adx_loader_status status;
    if(reload) {
        status = kosdec_adx_loader_step_reload(loader);
    } else {
        /* A single 512-byte step cannot sustain 44.1 kHz stereo at 60 Hz.
           Budget up to one sector, stopping at WAIT, backpressure or EOF. */
        for(unsigned i = 0; i < KOSDEC_ADX_REQUEST_SECTOR_BYTES / 512u; ++i) {
            status = kosdec_adx_loader_step_stream(loader);
            if(status != KOSDEC_ADX_LOADER_OK || !loader->active || loader->paused ||
               loader->request.state == KOSDEC_ADX_REQUEST_READEND)
                break;
        }
    }
    if(status == KOSDEC_ADX_LOADER_NEED_INPUT)
        return KOSDEC_ADX_NEED_INPUT;
    if(status == KOSDEC_ADX_LOADER_NEED_OUTPUT)
        return KOSDEC_ADX_NEED_OUTPUT;
    struct kosdec_adx_loader_info info;
    if(status != KOSDEC_ADX_LOADER_OK || !kosdec_adx_loader_get_info(loader,&info))
        return r->terminal = loader_error(r);
    if(reload && info.state == KOSDEC_ADX_LOADER_WAIT && r->linked.seal_when_empty) {
        r->joint_error = kosdec_adx_joint_finish(r->joint);
        if(r->joint_error != KOSDEC_ADX_JOINT_OK)
            return r->terminal = KOSDEC_ADX_IO_ERROR;
    }
    return KOSDEC_ADX_MORE;
}

static struct kosdec_adx_joint_read_result read_window(
    struct kosdec_adx_joint_reader *r, int16_t *output, size_t frames, size_t tail,
    bool separate_transport) {
    struct kosdec_adx_joint_read_result result = {
        KOSDEC_ADX_BAD_ARGUMENT, KOSDEC_ADX_JOINT_OK, 0};
    if(!valid(r) || (!output && frames) ||
       (output && (uintptr_t)output % _Alignof(int16_t))) return result;
    if(r->terminal) {
        result.status = r->terminal; result.joint_status = r->joint_error; return result;
    }
    enum kosdec_adx_joint_status s;
    if(r->linked.scan_active) {
        result = scan_link(r);
        s = result.joint_status;
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        if(result.status == KOSDEC_ADX_DONE) r->terminal = result.status;
        return result;
    }
    bool at_link_boundary = r->linked.completed_records && !r->decoder.header_used &&
                            !r->linked.header_selected;
    if(at_link_boundary && !r->linked.follow) {
        result.status = r->terminal = KOSDEC_ADX_DONE;
        return result;
    }
    bool sector_source = r->sector.request.read_at != NULL;
    bool has_feed = r->feed || (r->linked.loader && !separate_transport);
    bool eager_feed = has_feed && r->linked.enabled;
    bool output_blocked = !frames || (r->job.pcm && frames < 32);
    if(output_blocked && !sector_source && !eager_feed) {
        result.status = KOSDEC_ADX_NEED_OUTPUT; return result;
    }
    enum kosdec_adx_status transport = KOSDEC_ADX_NEED_OUTPUT;
    if(sector_source && !separate_transport &&
       (!r->decoder.header_ready || r->decoder.frames_done < r->decoder.total_frames ||
        r->job.trap)) {
        transport = media_adx_joint_sector_step(r);
        if(transport >= KOSDEC_ADX_DONE) {
            result.status = r->terminal = transport;
            result.joint_status = r->joint_error;
            return result;
        }
    }
    if(output_blocked && sector_source) {
        result.status = transport;
        return result;
    }
    if(sector_source && separate_transport && r->decoder.header_ready && !r->sector.configured) {
        result.status = KOSDEC_ADX_NEED_INPUT;
        return result;
    }
    struct kosdec_adx_joint_info info;
    s = kosdec_adx_joint_query(r->joint, &info);
    if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    bool feed_progress = false, retry_input = false;
    if(has_feed && !info.sealed) {
        enum kosdec_adx_joint_feed_status feed = step_feed(r, &retry_input);
        if(feed == KOSDEC_ADX_JOINT_FEED_END) {
            s = kosdec_adx_joint_finish(r->joint);
            if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        }
        else if(feed != KOSDEC_ADX_JOINT_FEED_MORE && feed != KOSDEC_ADX_JOINT_FEED_WAIT) {
            result.status = r->terminal = loader_error(r);
            return result;
        }
        feed_progress = feed == KOSDEC_ADX_JOINT_FEED_MORE;
        s = kosdec_adx_joint_query(r->joint, &info);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    }
    if(at_link_boundary && info.sealed && !r->held.bytes &&
       !info.available[KOSDEC_ADX_JOINT_DATA]) {
        result.status = r->terminal = KOSDEC_ADX_DONE;
        return result;
    }
    if(output_blocked) {
        result.status = feed_progress ? KOSDEC_ADX_MORE :
            retry_input ? KOSDEC_ADX_NEED_INPUT : KOSDEC_ADX_NEED_OUTPUT;
        return result;
    }
    if(r->job.pcm) {
        result = read_job(r, output, frames, tail, &info);
        s = result.joint_status;
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        if(sector_source && !result.frames && (result.status == KOSDEC_ADX_NEED_OUTPUT ||
                                               result.status == KOSDEC_ADX_NEED_INPUT)) {
            if(transport == KOSDEC_ADX_MORE ||
               (result.status == KOSDEC_ADX_NEED_OUTPUT && transport == KOSDEC_ADX_NEED_INPUT))
                result.status = transport;
        }
        if(eager_feed && feed_progress && result.status == KOSDEC_ADX_NEED_INPUT)
            result.status = KOSDEC_ADX_MORE;
        if(r->linked.scan_active) {
            s = begin_link_scan(r);
            if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        }
        if(result.status >= KOSDEC_ADX_DONE) r->terminal = result.status;
        return result;
    }
    if(!r->held.bytes) {
        size_t request = KOSDEC_ADX_HEADER_BUDGET;
        if(info.kind == KOSDEC_ADX_JOINT_QUEUE &&
           request < info.contiguous[KOSDEC_ADX_JOINT_DATA])
            request = info.contiguous[KOSDEC_ADX_JOINT_DATA];
        s = kosdec_adx_joint_get(r->joint, KOSDEC_ADX_JOINT_DATA, request, &r->held);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
        info.available[KOSDEC_ADX_JOINT_DATA] -= r->held.bytes;
    }
    bool header_pending = !r->decoder.header_ready;
    bool linked_footer = r->linked.enabled && !header_pending &&
                         r->decoder.frames_done == r->decoder.total_frames;
    const uint8_t *input = r->held.bytes ? r->held.data + r->used : NULL;
    size_t bytes = r->held.bytes - r->used;
    bool final_input = info.sealed && !info.available[KOSDEC_ADX_JOINT_DATA];
    struct kosdec_adx_result decoded = linked_footer ?
        read_footer(r, input, bytes, final_input) :
        kosdec_adx_decode(&r->decoder, input, bytes, output, frames, final_input);
    r->used += decoded.consumed; r->consumed_bytes += decoded.consumed;
    result.frames = decoded.frames; result.status = decoded.status;
    if(r->sfa_live && header_pending && r->decoder.header_ready)
        result.status = check_sfa_header(r);
    if(r->used == r->held.bytes) {
        s = release(r);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    }
    if(r->linked.enabled) {
        if(header_pending && r->decoder.header_ready)
            result.status = admit_linked_header(r);
        else if(linked_footer)
            result.status = complete_linked_record(r, result.status);
    }
    if(eager_feed && feed_progress && result.status == KOSDEC_ADX_NEED_INPUT)
        result.status = KOSDEC_ADX_MORE;
    if(r->linked.scan_active) {
        s = begin_link_scan(r);
        if(s != KOSDEC_ADX_JOINT_OK) goto joint_failure;
    }
    if(result.status >= KOSDEC_ADX_DONE) r->terminal = result.status;
    return result;
joint_failure:
    r->terminal = KOSDEC_ADX_IO_ERROR; r->joint_error = s;
    result.status = r->terminal; result.joint_status = s;
    return result;
}
struct kosdec_adx_joint_read_result kosdec_adx_joint_reader_read(
    struct kosdec_adx_joint_reader *r, int16_t *output, size_t frames) {
    return read_window(r, output, frames, 0, false);
}
struct kosdec_adx_joint_read_result media_adx_joint_reader_read_pipe(
    struct kosdec_adx_joint_reader *r, size_t room, size_t tail, bool separate_transport) {
    return read_window(r, r->job.pcm, room, tail, separate_transport);
}
void kosdec_adx_joint_reader_cancel(struct kosdec_adx_joint_reader *r) {
    if(valid(r) && !r->terminal) {
        kosdec_adx_cancel(&r->decoder); r->terminal = KOSDEC_ADX_CANCELLED;
    }
}
enum kosdec_adx_joint_status kosdec_adx_joint_reader_close(struct kosdec_adx_joint_reader *r) {
    if(!valid(r)) return KOSDEC_ADX_JOINT_BAD_ARGUMENT;
    enum kosdec_adx_joint_status s = unget_link_next(r);
    if(s == KOSDEC_ADX_JOINT_OK)
        s = r->linked.scan_active ? retire_job_input(r, r->used) : release(r);
    if(s == KOSDEC_ADX_JOINT_OK) s = media_adx_joint_sector_close(r);
    if(s == KOSDEC_ADX_JOINT_OK) *r = (struct kosdec_adx_joint_reader){0};
    return s;
}
