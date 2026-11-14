/* Original private loader; existing native SPU transport owns DMA and cache. */
#include "abk_upload.h"
#include <dc/sound/sound.h>
#include <kos/irq.h>
#include <errno.h>
#include <stddef.h>

static int error(abk_upload_t *t, int e, int fatal) {
    if(e <= 0)
        e = EIO;
    t->last_error = e;
    if(fatal && !t->first_error)
        t->first_error = e;
    if(fatal)
        t->state = t->request ? ABKU_RETIRING : ABKU_FAILED;
    errno = e;
    return -1;
}

static int context(abk_upload_t *t) {
    if(!t) {
        errno = EINVAL;
        return -1;
    }
    if(irq_inside_int()) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t ap = (uintptr_t)a, bp = (uintptr_t)b;
    if(an > UINTPTR_MAX - ap || bn > UINTPTR_MAX - bp)
        return 1;
    return an && bn && ap < bp + bn && bp < ap + an;
}

static int settled(abk_upload_t *t) {
    if(t->cancelled) {
        t->state = ABKU_CANCELLED;
        t->last_error = ECANCELED;
        errno = ECANCELED;
        return -1;
    }
    if(t->first_error) {
        t->state = ABKU_FAILED;
        errno = t->last_error = t->first_error;
        return -1;
    }
    t->state = t->image ? ABKU_IMAGE_READY : ABKU_READY;
    t->last_error = 0;
    return 0;
}

static int submit(abk_upload_t *t) {
    abk_placement_t chunk;
    if(abk_plan_chunk(&t->placement, t->committed, t->chunk_limit,
                      &chunk) != ABK_OK || !chunk.bytes)
        return error(t, EINVAL, 1);
    t->chunk_bytes = chunk.bytes;
    errno = 0;
    if(spu_memload_async(chunk.sound_ram_offset,
                        (const unsigned char *)t->data + chunk.source_file_offset,
                        chunk.bytes, SPU_TRANSFER_AUTO, t->lifetime_ms,
                        NULL, NULL, &t->request) < 0)
        return error(t, errno, 1);
    t->last_error = 0;
    return 0;
}

static int upload_begin(abk_upload_t *t, const void *data, uint32_t size,
                     abk_sample_t *samples, uint32_t sample_capacity,
                     abk_zone_t *zones, uint32_t zone_capacity,
                     uint32_t chunk_limit, uint32_t lifetime_ms, int image) {
    abk_plan_t plan;
    abk_result_t result;
    if(context(t) < 0)
        return -1;
    if(!data || !samples || !zones || size > ABK_MAX_FILE ||
       sample_capacity > ABK_MAX_SAMPLES || zone_capacity > ABK_MAX_ZONES ||
       chunk_limit < 32 || chunk_limit > 32768 || (chunk_limit & 31) ||
       !lifetime_ms || overlaps(t, sizeof(*t), data, size) ||
       overlaps(t, sizeof(*t), samples, sample_capacity * sizeof(*samples)) ||
       overlaps(t, sizeof(*t), zones, zone_capacity * sizeof(*zones))) {
        /* Invalid aliasing must not write through t into immutable input. */
        errno = EINVAL;
        return -1;
    }
    if(t->state != ABKU_EMPTY)
        return error(t, EALREADY, 0);
    result = abk_plan_build(data, size, &plan, samples, sample_capacity,
                            zones, zone_capacity);
    t->validation = result;
    if(result != ABK_OK)
        return error(t, EINVAL, 1);
    t->data = data;
    t->plan = plan;
    t->chunk_limit = chunk_limit;
    t->lifetime_ms = lifetime_ms;
    t->image = image;
    errno = 0;
    t->address = snd_mem_malloc(image ? size : plan.payload_bytes);
    if(!t->address)
        return error(t, errno ? errno : ENOMEM, 1);
    if((image && (t->address < 0x30000u || t->address >= 0x200000u ||
                  (t->address & 31u) || size > 0x200000u-t->address)) ||
       (!image && abk_plan_place(&plan, t->address, plan.payload_bytes,
                                 &t->placement) != ABK_OK)) {
        error(t, ERANGE, 1);
        /* No submission occurred. Preserve ownership if release reports error. */
        errno = 0;
        snd_mem_free(t->address);
        if(errno)
            return error(t, errno, 1);
        t->address = 0;
        return error(t, ERANGE, 1);
    }
    if(image)
        t->placement = (abk_placement_t){0, t->address, size};
    t->state = ABKU_UPLOADING;
    return submit(t);
}

int abk_upload_begin(abk_upload_t *t, const void *data, uint32_t size,
                     abk_sample_t *samples, uint32_t sample_capacity,
                     abk_zone_t *zones, uint32_t zone_capacity,
                     uint32_t chunk_limit, uint32_t lifetime_ms) {
    return upload_begin(t,data,size,samples,sample_capacity,zones,zone_capacity,
                         chunk_limit,lifetime_ms,0);
}
int abk_upload_begin_image(abk_upload_t *t, const void *data, uint32_t size,
                           abk_sample_t *samples, uint32_t sample_capacity,
                           abk_zone_t *zones, uint32_t zone_capacity,
                           uint32_t chunk_limit, uint32_t lifetime_ms) {
    return upload_begin(t,data,size,samples,sample_capacity,zones,zone_capacity,
                         chunk_limit,lifetime_ms,1);
}

int abk_upload_pump(abk_upload_t *t) {
    spu_transfer_status_t status;
    int exact;
    if(context(t) < 0)
        return -1;
    if(t->state == ABKU_CLOSED || t->state == ABKU_EMPTY ||
       t->state == ABKU_READY || t->state == ABKU_IMAGE_READY || t->state == ABKU_MOVED)
        return 0;
    if(!t->request)
        return settled(t);
    errno = 0;
    if(spu_transfer_get_status(t->request, &status) < 0)
        return error(t, errno, 1);
    switch(status.state) {
        case SPU_TRANSFER_QUEUED:
        case SPU_TRANSFER_RUNNING:
            if(t->cancelled || t->first_error) {
                errno = 0;
                if(spu_transfer_cancel(t->request) < 0 && errno != EALREADY)
                    return error(t, errno, 1);
            }
            t->last_error = 0;
            return 0;
        case SPU_TRANSFER_COMPLETE:
        case SPU_TRANSFER_CANCELLED:
        case SPU_TRANSFER_TIMED_OUT:
        case SPU_TRANSFER_ERROR:
            break;
        default:
            return error(t, EIO, 1); /* Unknown means retained, not terminal. */
    }
    exact = status.state == SPU_TRANSFER_COMPLETE && status.result == 0 &&
            status.direction == SPU_TRANSFER_TO_SOUND_RAM &&
            status.requested_bytes == t->chunk_bytes &&
            status.completed_bytes == t->chunk_bytes;
    if(!exact) {
        int e = status.result;
        if(!e)
            e = status.state == SPU_TRANSFER_CANCELLED ? ECANCELED :
                status.state == SPU_TRANSFER_TIMED_OUT ? ETIMEDOUT : EIO;
        error(t, e, 1);
    }
    errno = 0;
    if(spu_transfer_destroy(t->request) < 0)
        return error(t, errno, errno != EBUSY);
    t->request = NULL;
    if(exact && !t->first_error && !t->cancelled)
        t->committed += t->chunk_bytes;
    t->chunk_bytes = 0;
    if(t->first_error || t->cancelled || t->committed == t->placement.bytes)
        return settled(t);
    return submit(t);
}

int abk_upload_cancel(abk_upload_t *t) {
    if(context(t) < 0)
        return -1;
    if(t->state == ABKU_CLOSED || t->state == ABKU_MOVED)
        return 0;
    t->cancelled = 1;
    t->state = t->request ? ABKU_RETIRING : ABKU_CANCELLED;
    if(t->request) {
        errno = 0;
        if(spu_transfer_cancel(t->request) < 0 && errno != EALREADY)
            return error(t, errno, 1);
    }
    t->last_error = 0;
    return 0;
}

int abk_upload_close(abk_upload_t *t) {
    if(context(t) < 0)
        return -1;
    if(t->state == ABKU_CLOSED)
        return 0;
    if(t->request)
        return error(t, EBUSY, 0);
    if(t->address) {
        errno = 0;
        snd_mem_free(t->address);
        if(errno)
            return error(t, errno, 1);
        t->address = 0;
    }
    t->data = NULL;
    t->state = ABKU_CLOSED;
    t->last_error = 0;
    return 0;
}
