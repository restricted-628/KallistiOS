/* Caller-serviced provider for whole-sector publication and loop prefetch. */
#include "adx_joint_reader_internal.h"

#define SECTOR KOSDEC_ADX_REQUEST_SECTOR_BYTES

static uint32_t sectors(uint32_t bytes) {
    return bytes / SECTOR + (bytes % SECTOR != 0);
}

static enum kosdec_adx_status sector_eof(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_request *req = &r->sector.request;
    if(!r->job.trap || req->cursor != r->sector.eof_sector)
        return KOSDEC_ADX_MORE;
    if(r->job.repeat) {
        if(kosdec_adx_request_seek(req, r->decoder.loop.start_byte / SECTOR,
                                  KOSDEC_ADX_REQUEST_SET) != KOSDEC_ADX_REQUEST_STOPPED)
            return KOSDEC_ADX_IO_ERROR;
    }
    else {
        r->job.trap = false;
        r->sector.eof_sector = req->sectors;
    }
    return KOSDEC_ADX_MORE;
}

static enum kosdec_adx_status configure(struct kosdec_adx_joint_reader *r) {
    const struct kosdec_adx_loop *loop = &r->decoder.loop;
    if(loop->present) {
        if(loop->end_byte > r->sector.request.input_bytes)
            return KOSDEC_ADX_TRUNCATED;
        r->sector.eof_sector = sectors(loop->end_byte);
        r->sector.padding = (SECTOR - loop->end_byte % SECTOR) % SECTOR;
        r->job.trap = true;
    }
    else r->sector.eof_sector = r->sector.request.sectors;
    r->sector.configured = true;
    /* The request that supplied the last header byte can already have
       reached the first loop boundary. Resolve it before decoding PCM. */
    return sector_eof(r);
}

static enum kosdec_adx_status issue(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_request *req = &r->sector.request;
    struct kosdec_adx_joint_info info;
    r->joint_error = kosdec_adx_joint_query(r->joint, &info);
    if(r->joint_error != KOSDEC_ADX_JOINT_OK) return KOSDEC_ADX_IO_ERROR;
    if(req->cursor == req->sectors) {
        if(!info.sealed) {
            r->joint_error = kosdec_adx_joint_finish(r->joint);
            if(r->joint_error != KOSDEC_ADX_JOINT_OK) return KOSDEC_ADX_IO_ERROR;
        }
        return KOSDEC_ADX_NEED_OUTPUT;
    }
    /* Parse the header before issuing sectors past it. The first sector can
       contain both the header and some audio. The decoder owns that suffix. */
    uint32_t limit = r->sector.configured ? r->sector.eof_sector :
        sectors(r->decoder.header_bytes);
    if(req->cursor >= limit) return KOSDEC_ADX_NEED_OUTPUT;
    uint32_t count = limit - req->cursor;
    if(count > req->sectors - req->cursor) count = req->sectors - req->cursor;
    if(count > r->sector.sectors_per_read) count = r->sector.sectors_per_read;
    size_t contiguous = info.contiguous[KOSDEC_ADX_JOINT_FREE] / SECTOR;
    if(contiguous < count) count = (uint32_t)contiguous;
    if(!count) return KOSDEC_ADX_NEED_OUTPUT;

    size_t bytes = (size_t)count * SECTOR;
    r->joint_error = kosdec_adx_joint_get(r->joint, KOSDEC_ADX_JOINT_FREE,
        bytes, &r->sector.held);
    if(r->joint_error != KOSDEC_ADX_JOINT_OK || r->sector.held.bytes != bytes ||
       kosdec_adx_request_start(req, count, r->sector.held.data, bytes) != KOSDEC_ADX_REQUEST_READING)
        return KOSDEC_ADX_IO_ERROR;
    return KOSDEC_ADX_MORE;
}

enum kosdec_adx_status media_adx_joint_sector_step(struct kosdec_adx_joint_reader *r) {
    struct kosdec_adx_request *req = &r->sector.request;
    if(r->decoder.header_ready && !r->sector.configured) {
        enum kosdec_adx_status status = configure(r);
        if(status != KOSDEC_ADX_MORE) return status;
    }
    if(!r->sector.held.bytes) return issue(r);
    switch(kosdec_adx_request_step(req)) {
    case KOSDEC_ADX_REQUEST_READING: return KOSDEC_ADX_MORE;
    case KOSDEC_ADX_REQUEST_WAIT: return KOSDEC_ADX_NEED_INPUT;
    case KOSDEC_ADX_REQUEST_READEND: break;
    case KOSDEC_ADX_REQUEST_TRUNCATED: return KOSDEC_ADX_TRUNCATED;
    default: return KOSDEC_ADX_IO_ERROR;
    }
    r->joint_error = kosdec_adx_joint_put(r->joint, &r->sector.held);
    if(r->joint_error != KOSDEC_ADX_JOINT_OK) return KOSDEC_ADX_IO_ERROR;
    r->sector.held = (struct kosdec_adx_chunk){0};

    /* End-of-range decisions act on the provider cursor. Buffered DATA from
       earlier requests remains in order for the decoder's later sample trap. */
    enum kosdec_adx_status status = sector_eof(r);
    if(status != KOSDEC_ADX_MORE) return status;
    status = issue(r);
    return status == KOSDEC_ADX_NEED_OUTPUT ? KOSDEC_ADX_MORE : status;
}

enum kosdec_adx_status media_adx_joint_sector_step_stream(struct kosdec_adx_joint_reader *r) {
    enum kosdec_adx_status status = KOSDEC_ADX_NEED_OUTPUT;
    /* Bound owner-side I/O to at most one sector of provider calls per pass.
       Admission can consume one step without calling the provider. */
    for(unsigned i = 0; i < SECTOR / 512u; ++i) {
        status = media_adx_joint_sector_step(r);
        if(status != KOSDEC_ADX_MORE || r->joint->sealed)
            break;
    }
    if(status >= KOSDEC_ADX_DONE)
        r->terminal = status;
    return status;
}

enum kosdec_adx_joint_status media_adx_joint_sector_close(struct kosdec_adx_joint_reader *r) {
    if(!r->sector.request.read_at) return KOSDEC_ADX_JOINT_OK;
    if(r->sector.held.bytes) {
        if(kosdec_adx_request_stop(&r->sector.request) != KOSDEC_ADX_REQUEST_STOPPED)
            return KOSDEC_ADX_JOINT_BUSY;
        enum kosdec_adx_joint_status status =
            kosdec_adx_joint_unget(r->joint, &r->sector.held);
        if(status != KOSDEC_ADX_JOINT_OK) return status;
        r->sector.held = (struct kosdec_adx_chunk){0};
    }
    return KOSDEC_ADX_JOINT_OK;
}
