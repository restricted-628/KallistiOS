/* KallistiOS ##version##

   pvr_scene.c
   Copyright (C) 2002,2004 Megan Potter
   Copyright (C) 2024 Falco Girgis
   Copyright (C) 2026 Troy Davis
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arch/irq.h>
#include <kos/dbglog.h>
#include <kos/genwait.h>
#include <kos/regfield.h>
#include <kos/thread.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <dc/sq.h>
#include "pvr_internal.h"

/* FIXME: NDEBUG is a reserved C macro, we shouldn't use it like that... */
#ifdef NDEBUG
#  define PVR_DEBUG 0
#else
#  define PVR_DEBUG 1
#endif

/*

   Scene rendering

   Please see ../../include/dc/pvr.h for more info on this API!

*/

static void set_next_full_pixel_clip(uint32_t width, uint32_t height) {
    pvr_state.next_pclip_left = 0;
    pvr_state.next_pclip_top = 0;
    pvr_state.next_pclip_right = width - 1u;
    pvr_state.next_pclip_bottom = height - 1u;
    pvr_state.next_pclip_x = (pvr_state.next_pclip_right << 16);
    pvr_state.next_pclip_y = (pvr_state.next_pclip_bottom << 16);
}

static void set_next_default_background(uint32_t width, uint32_t height) {
    pvr_state.next_background = (pvr_background_plane_t) {
        .depth = pvr_state.zclip,
        .vertices = {
            { 0.0f, (float)height, FLT_EPSILON, pvr_state.bg_color },
            { 0.0f, 0.0f, FLT_EPSILON, pvr_state.bg_color },
            { (float)width, (float)height, FLT_EPSILON,
              pvr_state.bg_color }
        }
    };
}

static void *set_vertbuf_unchecked(pvr_list_t list, void *buffer, size_t len) {
    void *oldbuf;

    // Save the old value.
    oldbuf = pvr_state.dma_buffers[0].base[list];

    // Write new values.
    pvr_state.dma_buffers[0].base[list] = (uint8_t *)buffer;
    pvr_state.dma_buffers[0].ptr[list] = 0;
    pvr_state.dma_buffers[0].size[list] = len / 2;
    pvr_state.dma_buffers[0].flushed &= ~BIT(list);
    pvr_state.dma_buffers[0].ready = 0;
    pvr_state.dma_buffers[1].base[list] = ((uint8_t *)buffer) + len / 2;
    pvr_state.dma_buffers[1].ptr[list] = 0;
    pvr_state.dma_buffers[1].size[list] = len / 2;
    pvr_state.dma_buffers[1].flushed &= ~BIT(list);
    pvr_state.dma_buffers[1].ready = 0;

    return oldbuf;
}

void *pvr_set_vertbuf(pvr_list_t list, void *buffer, size_t len) {
    void *oldbuf;
    int old_irq;

    // Make sure we have global DMA usage enabled. The DMA can still
    // be used in other situations, but the user must take care of
    // that themselves.
    assert(pvr_state.dma_mode);

    // Make sure it's an _enabled_ list.
    assert(list >= PVR_LIST_OP_POLY && list <= PVR_LIST_PT_POLY);
    assert(pvr_state.lists_enabled & BIT(list));

    // Make sure the buffer parameters are valid.
    assert(__is_aligned(buffer, 32));
    assert(len >= 128 && !(len & 63));

    old_irq = irq_disable();
    oldbuf = set_vertbuf_unchecked(list, buffer, len);
    irq_restore(old_irq);

    return oldbuf;
}

int pvr_set_vertbuf_checked(pvr_list_t list, void *buffer, size_t len,
                            void **old_buffer) {
    void *oldbuf;
    int old_irq;

    if(list < PVR_LIST_OP_POLY || list > PVR_LIST_PT_POLY || !buffer
       || !__is_aligned(buffer, 32) || len < 128 || (len & 63)) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.dma_mode) {
        irq_restore(old_irq);
        errno = EPERM;
        return -1;
    }

    if(!(pvr_state.lists_enabled & BIT(list))) {
        irq_restore(old_irq);
        errno = EINVAL;
        return -1;
    }

    /* A queued RAM frame may still contain offsets into the old allocation,
       even after the application's scene has ended. */
    if(pvr_state.scene_active || pvr_state.dma_buffers[0].ready
       || pvr_state.dma_buffers[1].ready) {
        irq_restore(old_irq);
        errno = EBUSY;
        return -1;
    }

    oldbuf = set_vertbuf_unchecked(list, buffer, len);

    if(old_buffer)
        *old_buffer = oldbuf;

    irq_restore(old_irq);
    return 0;
}

void *pvr_vertbuf_tail(pvr_list_t list) {
    uint8_t *bufbase;

    // Check the validity of the request.
    assert(pvr_state.dma_mode);

    // Get the buffer base.
    bufbase = pvr_state.dma_buffers[pvr_state.ram_target].base[list];
    assert(bufbase);

    // Return the current end of the buffer.
    return bufbase + pvr_state.dma_buffers[pvr_state.ram_target].ptr[list];
}

void pvr_vertbuf_written(pvr_list_t list, size_t amt) {
    uint32_t val;

    // Check the validity of the request.
    assert(pvr_state.dma_mode);

    // Change the current end of the buffer.
    val = pvr_state.dma_buffers[pvr_state.ram_target].ptr[list];
    val += amt;
    assert(val < pvr_state.dma_buffers[pvr_state.ram_target].size[list]);
    pvr_state.dma_buffers[pvr_state.ram_target].ptr[list] = val;
}

static void pvr_start_ta_rendering(void) {
    // Make sure to wait until the TA is ready to start rendering a new scene
    if(!pvr_state.ta_checked_ready) {
        pvr_wait_ready();

        // If using a single vertex buffer, we have to wait until the PVR is
        // done rendering to use the TA again.
        if(!pvr_state.vbuf_doublebuf)
            pvr_wait_render_done();

        pvr_state.ta_checked_ready = 1;
        pvr_state.curr_to_texture = pvr_state.next_to_texture;
        pvr_state.to_txr_rp = pvr_state.next_to_txr_rp;
        pvr_state.to_txr_w = pvr_state.next_to_txr_w;
        pvr_state.to_txr_h = pvr_state.next_to_txr_h;
        pvr_state.to_txr_stride_px = pvr_state.next_to_txr_stride_px;
        pvr_state.to_txr_addr = pvr_state.next_to_txr_addr;
        pvr_state.curr_pclip_x = pvr_state.next_pclip_x;
        pvr_state.curr_pclip_y = pvr_state.next_pclip_y;
        pvr_state.curr_background = pvr_state.next_background;

        // Starting from that point, we consider that the Tile Accelerator
        // might be busy.
        pvr_state.ta_busy = 1;
        pvr_status_advance();
    }
}

/* Begin collecting data for a frame of 3D output to the off-screen
   frame buffer */
void pvr_scene_begin(void) {
    int i;

    if(pvr_state.multipass) {
        pvr_state.multipass->build_pass = 0;
        pvr_state.multipass->ta_pass = 0;
        pvr_state.multipass->fault_sequence =
            pvr_state.fault_status.sequence;
        pvr_activate_pass(0);
    }

    pvr_state.next_to_texture = 0;
    set_next_full_pixel_clip((uint32_t)vid_mode->width,
                             (uint32_t)vid_mode->height);
    set_next_default_background((uint32_t)vid_mode->width,
                                (uint32_t)vid_mode->height);
    pvr_state.scene_active = true;
    pvr_state.ta_checked_ready = 0;
    pvr_state.lists_closed = 0;

    // Get general stuff ready.
    pvr_state.list_reg_open = PVR_LIST_NONE;

    // Clear these out in case we're using DMA.
    if(pvr_state.dma_mode) {
        pvr_state.dma_buffers[pvr_state.ram_target].flushed = 0;

        for(i = 0; i < PVR_OPB_COUNT; i++) {
            pvr_state.dma_buffers[pvr_state.ram_target].ptr[i] = 0;
        }

        pvr_sync_stats(PVR_SYNC_BUFSTART);
    }
    else {
        // We assume registration is starting immediately
        pvr_sync_stats(PVR_SYNC_REGSTART);
    }

    pvr_status_advance();
}

static int finish_direct_pass_lists(void) {
    int i;

    if(pvr_state.list_reg_open != PVR_LIST_NONE && pvr_list_finish() < 0)
        return -1;

    /* Every enabled list must contribute an end marker before continuation or
       final rendering. Empty lists receive the established blank header. */
    for(i = 0; i < PVR_OPB_COUNT; ++i) {
        if((pvr_state.lists_enabled & BIT(i)) &&
                !(pvr_state.lists_closed & BIT(i))) {
            if(pvr_list_begin(i) < 0)
                return -1;

            pvr_blank_polyhdr(i);

            if(pvr_list_finish() < 0)
                return -1;
        }
    }

    return 0;
}

int pvr_scene_next_pass(void) {
    pvr_multipass_state_t *multipass = pvr_state.multipass;
    volatile pvr_ta_buffers_t *buffer;
    size_t next_pass;
    int old_irq;
    int wait_result = 0;

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!multipass) {
        errno = ENOTSUP;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(pvr_state.dma_mode) {
        errno = ENOTSUP;
        return -1;
    }

    if(multipass->build_pass + 1u >= multipass->pass_count) {
        errno = EALREADY;
        return -1;
    }

    if(finish_direct_pass_lists() < 0)
        return -1;

    old_irq = irq_disable();

    if(pvr_state.lists_transferred != pvr_state.lists_enabled) {
        wait_result = genwait_wait((void *)&pvr_state.lists_transferred,
                                   "PVR multipass boundary", 100);
    }

    if(wait_result < 0) {
        bool faulted = multipass->fault_sequence !=
            pvr_state.fault_status.sequence;

        irq_restore(old_irq);
        errno = faulted ? EIO : ETIMEDOUT;
        return -1;
    }

    if(multipass->fault_sequence != pvr_state.fault_status.sequence) {
        irq_restore(old_irq);
        errno = EIO;
        return -1;
    }

    /* The IRQ cannot complete another list while the continuation registers
       and their software masks are transitioned as one operation. */
    next_pass = multipass->build_pass + 1u;
    multipass->build_pass = next_pass;
    multipass->ta_pass = next_pass;
    pvr_activate_pass(next_pass);
    pvr_state.lists_transferred = 0;
    pvr_state.lists_closed = 0;
    pvr_state.list_reg_open = PVR_LIST_NONE;

    buffer = pvr_state.ta_buffers + pvr_state.ta_target;
    PVR_SET(PVR_TA_OPB_START, buffer->opb +
            multipass->layout.pass_opb_offset[next_pass]);
    PVR_SET(PVR_OPB_CFG, multipass->list_reg_mask[next_pass]);
    PVR_SET(PVR_TA_LIST_CONT, BIT(31));
    (void)PVR_GET(PVR_TA_LIST_CONT);

    pvr_status_advance();
    irq_restore(old_irq);
    return 0;
}

void pvr_scene_begin_txr(pvr_ptr_t txr, uint32_t *rx, uint32_t *ry) {
    (void)ry;

    (void)pvr_scene_begin_rtt(txr, pvr_state.w, pvr_state.h, *rx);
}

int pvr_scene_begin_rtt(pvr_ptr_t txr, uint32_t render_w,
                        uint32_t render_h, uint32_t stride_px) {
    if(!txr || render_w == 0 || render_h == 0 || stride_px < render_w ||
            (stride_px & 3))
        return -1;

    /* The existing render-to-texture path programs a 16-bit render pitch in
       64-bit units. Keep that behavior explicit for the sized RTT API. */
    pvr_state.next_to_txr_rp = stride_px * 2 / 8;
    pvr_state.next_to_txr_w = render_w;
    pvr_state.next_to_txr_h = render_h;
    pvr_state.next_to_txr_stride_px = stride_px;
    pvr_state.next_to_txr_addr = (uint32_t)(txr) - PVR_RAM_INT_BASE;

    pvr_scene_begin();

    pvr_state.next_to_texture = 1;
    set_next_full_pixel_clip(render_w, render_h);
    set_next_default_background(render_w, render_h);
    pvr_status_advance();

    return 0;
}

int pvr_scene_set_pixel_clip(const pvr_pixel_clip_t *clip) {
    uint32_t target_width, target_height;

    if(!clip) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(pvr_state.ta_checked_ready) {
        errno = EBUSY;
        return -1;
    }

    target_width = pvr_state.next_to_texture ?
        pvr_state.next_to_txr_w : (uint32_t)vid_mode->width;
    target_height = pvr_state.next_to_texture ?
        pvr_state.next_to_txr_h : (uint32_t)vid_mode->height;

    if(clip->left > clip->right || clip->top > clip->bottom ||
            clip->right >= target_width || clip->bottom >= target_height) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.next_to_texture && vid_mode->pm == PM_RGB888P &&
            ((clip->left | clip->top | clip->right | clip->bottom) & 1u)) {
        errno = EINVAL;
        return -1;
    }

    pvr_state.next_pclip_left = clip->left;
    pvr_state.next_pclip_top = clip->top;
    pvr_state.next_pclip_right = clip->right;
    pvr_state.next_pclip_bottom = clip->bottom;
    pvr_state.next_pclip_x = (clip->right << 16) | clip->left;
    pvr_state.next_pclip_y = (clip->bottom << 16) | clip->top;
    pvr_status_advance();

    return 0;
}

int pvr_scene_get_pixel_clip(pvr_pixel_clip_t *clip) {
    if(!clip) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    clip->left = pvr_state.next_pclip_left;
    clip->top = pvr_state.next_pclip_top;
    clip->right = pvr_state.next_pclip_right;
    clip->bottom = pvr_state.next_pclip_bottom;

    return 0;
}

int pvr_scene_set_background_plane(const pvr_background_plane_t *plane) {
    unsigned int i;

    if(!plane) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(pvr_state.ta_checked_ready) {
        errno = EBUSY;
        return -1;
    }

    if(!isfinite(plane->depth) || plane->depth <= 0.0f) {
        errno = EINVAL;
        return -1;
    }

    for(i = 0; i < 3; ++i) {
        const pvr_background_vertex_t *vertex = &plane->vertices[i];

        if(!isfinite(vertex->x) || !isfinite(vertex->y) ||
                !isfinite(vertex->z) || vertex->z <= 0.0f ||
                (vertex->color & 0xff000000u)) {
            errno = EINVAL;
            return -1;
        }
    }

    memcpy((void *)&pvr_state.next_background, plane, sizeof(*plane));
    pvr_status_advance();
    return 0;
}

int pvr_scene_get_background_plane(pvr_background_plane_t *plane) {
    if(!plane) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    memcpy(plane, (const void *)&pvr_state.next_background, sizeof(*plane));
    return 0;
}

int pvr_user_clip_compile(pvr_poly_hdr_t *command, pvr_list_t list,
                          const pvr_user_clip_t *clip) {
    if(!command || !clip || list < PVR_LIST_OP_POLY ||
            list > PVR_LIST_PT_POLY || clip->left > clip->right ||
            clip->top > clip->bottom || clip->right > PVR_USER_CLIP_MAX_X ||
            clip->bottom > PVR_USER_CLIP_MAX_Y) {
        errno = EINVAL;
        return -1;
    }

    memset(command, 0, sizeof(*command));
    command->cmd = PVR_CMD_USERCLIP | FIELD_PREP(PVR_TA_CMD_TYPE, list);
    command->start_x = clip->left;
    command->start_y = clip->top;
    command->end_x = clip->right;
    command->end_y = clip->bottom;

    return 0;
}

int pvr_user_clip_submit(pvr_list_t list, const pvr_user_clip_t *clip) {
    pvr_poly_hdr_t command;
    uint32_t target_width, target_height;
    uint32_t tile_width, tile_height;
    volatile pvr_dma_buffers_t *buffer;

    if(!pvr_state.valid) {
        errno = ENODEV;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(pvr_user_clip_compile(&command, list, clip) < 0)
        return -1;

    if(!(pvr_state.lists_enabled & BIT(list))) {
        errno = ENODEV;
        return -1;
    }

    target_width = pvr_state.next_to_texture ?
        pvr_state.next_to_txr_w : (uint32_t)vid_mode->width;
    target_height = pvr_state.next_to_texture ?
        pvr_state.next_to_txr_h : (uint32_t)vid_mode->height;
    tile_width = (target_width + 31u) / 32u;
    tile_height = (target_height + 31u) / 32u;

    if(clip->right >= tile_width || clip->bottom >= tile_height) {
        errno = EINVAL;
        return -1;
    }

    buffer = pvr_state.dma_buffers + pvr_state.ram_target;
    if(pvr_state.dma_mode && buffer->base[list])
        return pvr_list_prim(list, &command, sizeof(command));

    if(pvr_state.list_reg_open != list) {
        errno = EBUSY;
        return -1;
    }

    return pvr_prim(&command, sizeof(command));
}

static bool pvr_list_dma;

inline static bool pvr_list_uses_dma(pvr_list_t list) {
    return pvr_state.dma_mode &&
           pvr_state.dma_buffers[pvr_state.ram_target].base[list];
}

/* Begin collecting data for the given list type. Lists do not have to be
   submitted in any particular order, but all types of a list must be
   submitted at once. If the given list has already been closed, then an
   error (-1) is returned. */
int pvr_list_begin(pvr_list_t list) {
    volatile pvr_dma_buffers_t *b;

    if(list < PVR_LIST_OP_POLY || list > PVR_LIST_PT_POLY) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(!(pvr_state.lists_enabled & BIT(list))) {
        errno = ENODEV;
        return -1;
    }

    b = pvr_state.dma_buffers + pvr_state.ram_target;

    if(pvr_state.dma_mode && b->flushed & BIT(list)) {
        errno = EALREADY;
        return -1;
    }

    /* Check to make sure we can do this */
    if(PVR_DEBUG && !pvr_state.dma_mode && pvr_state.lists_closed & BIT(list)) {
        dbglog(DBG_WARNING, "pvr_list_begin: attempt to open already closed list\n");
        return -1;
    }

    /* If we already had a list open, close it first */
    if(pvr_state.list_reg_open != PVR_LIST_NONE && pvr_state.list_reg_open != list)
        pvr_list_finish();

    pvr_list_dma = pvr_list_uses_dma(list);

    if(!pvr_list_dma) {
        pvr_start_ta_rendering();
        sq_lock((void *)PVR_TA_INPUT);
    }

    /* Ok, set the flag */
    pvr_state.list_reg_open = list;
    pvr_status_advance();

    return 0;
}

/* End collecting data for the current list type. Lists can never be opened
   again within a single frame once they have been closed. Thus submitting
   a primitive that belongs in a closed list is considered an error. Closing
   a list that is already closed is also an error (-1). Note that if you open
   a list but do not submit any primitives, this causes a hardware error. For
   simplicity we just always submit a blank primitive. */
int pvr_list_finish(void) {
    /* Check to make sure we can do this */
    if(PVR_DEBUG && !pvr_state.dma_mode && pvr_state.list_reg_open == PVR_LIST_NONE) {
        dbglog(DBG_WARNING, "pvr_list_finish: attempt to close unopened list\n");
        return -1;
    }

    /* Check for immediate submission:
       A. If we are not in DMA mode, we must be submitting polygons
          immediately.
       B. If we are in DMA mode, yet there's no vertex buffer associated
          with the list type, assume we're doing hybrid drawing and
          are directly submitting this list type. */
    if(!pvr_list_dma) {
        /* In case we haven't sent anything in this list, send a dummy */
        pvr_blank_polyhdr(pvr_state.list_reg_open);

        sq_unlock();

        /* Set the flags */
        pvr_state.lists_closed |= BIT(pvr_state.list_reg_open);

        /* Send an EOL marker */
        pvr_sq_set32((void *)0, 0, 32, PVR_DMA_TA);
    }

    pvr_state.list_reg_open = PVR_LIST_NONE;
    pvr_status_advance();

    return 0;
}

int pvr_prim(const void *data, size_t size) {
    /* Check to make sure we can do this */
    if(PVR_DEBUG && pvr_state.list_reg_open == PVR_LIST_NONE) {
        dbglog(DBG_WARNING, "pvr_prim: attempt to submit to unopened list\n");
        return -1;
    }

    if(!pvr_list_dma) {
        if(PVR_DEBUG && ((uintptr_t)data & 0x7)) {
            dbglog(DBG_WARNING, "pvr_prim: attempt to submit data unaligned "
                                "to 8 bytes.\n");
            return -1;
        }

        /* Immediately send data via SQs. */
        sq_fast_cpy(SQ_MASK_DEST(PVR_TA_INPUT), data, size >> 5);
    }
    /* Defer data to RAM buffer for DMA-ing later. */
    else return pvr_list_prim(pvr_state.list_reg_open, data, size);

    return 0;
}

int pvr_list_prim(pvr_list_t list, const void *data, size_t size) {
    volatile pvr_dma_buffers_t * b;

    if(list < PVR_LIST_OP_POLY || list > PVR_LIST_PT_POLY || !data ||
            !size || (size & 31)) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    b = pvr_state.dma_buffers + pvr_state.ram_target;

    if(!pvr_state.dma_mode || !b->base[list]) {
        errno = ENODEV;
        return -1;
    }

    if(b->flushed & BIT(list)) {
        errno = EALREADY;
        return -1;
    }

    if((uintptr_t)data & 0x3) {
        errno = EFAULT;
        return -1;
    }

    if(b->ptr[list] > b->size[list] ||
            size > b->size[list] - b->ptr[list]) {
        errno = ENOSPC;
        return -1;
    }

    memcpy(b->base[list] + b->ptr[list], data, size);
    b->ptr[list] += size;

    return 0;
}

int pvr_list_flush(pvr_list_t list) {
    volatile pvr_dma_buffers_t *b;
    uint32_t old_ptr;
    size_t transfer_size;
    int rv;

    if(list < PVR_LIST_OP_POLY || list > PVR_LIST_PT_POLY) {
        errno = EINVAL;
        return -1;
    }

    if(!pvr_state.valid || !pvr_state.dma_mode) {
        errno = ENOTSUP;
        return -1;
    }

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    b = pvr_state.dma_buffers + pvr_state.ram_target;

    if(!b->base[list]) {
        errno = ENODEV;
        return -1;
    }

    if(b->flushed & BIT(list)) {
        errno = EALREADY;
        return -1;
    }

    if(pvr_state.list_reg_open != PVR_LIST_NONE &&
            pvr_state.list_reg_open != list) {
        errno = EBUSY;
        return -1;
    }

    old_ptr = b->ptr[list];

    /* An empty list still needs one parameter block before its delimiter. */
    if(!old_ptr) {
        if(b->size[list] < 64) {
            errno = ENOSPC;
            return -1;
        }

        pvr_blank_polyhdr_buf(list,
                              (pvr_poly_hdr_t *)(b->base[list] + old_ptr));
        b->ptr[list] += 32;
    }

    if(b->ptr[list] > b->size[list] ||
            b->size[list] - b->ptr[list] < 32) {
        b->ptr[list] = old_ptr;
        errno = ENOSPC;
        return -1;
    }

    /* A zero parameter block is the TA end-of-list delimiter. */
    memset(b->base[list] + b->ptr[list], 0, 32);
    b->ptr[list] += 32;
    transfer_size = b->ptr[list];

    pvr_state.list_reg_open = PVR_LIST_NONE;
    pvr_start_ta_rendering();

    sem_wait((semaphore_t *)&pvr_state.dma_lock);
    rv = pvr_dma_load_ta(b->base[list], transfer_size, true, NULL, NULL);
    sem_signal((semaphore_t *)&pvr_state.dma_lock);

    if(rv < 0) {
        b->ptr[list] = old_ptr;
        return -1;
    }

    /* This state belongs to the RAM frame, not the global scene. The next
       scene may begin while this frame is still progressing through the TA. */
    b->flushed |= BIT(list);
    pvr_state.lists_closed |= BIT(list);
    pvr_status_advance();

    return 0;
}

/* Call this after you have finished submitting all data for a frame; once
   this has been called, you can not submit any more data until one of the
   pvr_scene_begin() functions is called again. An error (-1) is returned if
   you have not started a scene already. */
int pvr_scene_finish(void) {
    int i, o;
    volatile pvr_dma_buffers_t *b;

    if(!pvr_state.scene_active) {
        errno = EPERM;
        return -1;
    }

    if(pvr_state.multipass &&
            pvr_state.multipass->build_pass + 1u !=
            pvr_state.multipass->pass_count) {
        errno = EPERM;
        return -1;
    }

    // If we're in DMA mode, then this works a little differently...
    if(pvr_state.dma_mode) {
        // If any enabled lists are empty, fill them with a blank polyhdr. Also
        // add a zero-marker to the end of each list.
        b = pvr_state.dma_buffers + pvr_state.ram_target;

        for(i = 0; i < PVR_OPB_COUNT; i++) {
            /* We never enabled the list globally with pvr_init() - skip it */
            if(!(pvr_state.lists_enabled & BIT(i)))
                continue;

            /* pvr_list_flush() already sent both this list and its delimiter.
               Sending it again here would duplicate every primitive. */
            if(b->flushed & BIT(i))
                continue;

            /* If any lists weren't used in this scene, submit blank ones now */
            if(!(pvr_state.lists_closed & BIT(i))) {
                pvr_list_begin(i);
                pvr_blank_polyhdr(i);
                pvr_list_finish();
            }

            /* We never associated an in-RAM DMA vertex buffer with the given
               list type, because we're using hybrid rendering and submitted
               that list type directly - skip it */
            if(!b->base[i])
                continue;

            // Make sure there's at least one primitive in each.
            if(b->ptr[i] == 0) {
                pvr_blank_polyhdr_buf(i, (pvr_poly_hdr_t*)(b->base[i]));
                b->ptr[i] += 32;
            }

            // Put a zero-marker on the end.
            memset(b->base[i] + b->ptr[i], 0, 32);
            b->ptr[i] += 32;

            // Verify that there is no overrun.
            assert(b->ptr[i] <= b->size[i]);
        }

        pvr_start_ta_rendering();

        // Flip buffers and mark them complete.
        o = irq_disable();
        pvr_state.dma_buffers[pvr_state.ram_target].ready = 1;
        pvr_state.ram_target ^= 1;
        irq_restore(o);

        pvr_sync_stats(PVR_SYNC_BUFDONE);

        pvr_start_dma();
    }
    else {
        if(finish_direct_pass_lists() < 0)
            return -1;
    }

    pvr_state.scene_active = false;
    pvr_status_advance();

    /* Ok, now it's just a matter of waiting for the interrupt... */
    return 0;
}

int pvr_wait_ready(void) {
    int flags, t = 0;

    assert(pvr_state.valid);

    flags = irq_disable();

    if(pvr_state.ta_busy)
        t = genwait_wait((void *)&pvr_state.ta_busy, "PVR wait ready", 100);

    irq_restore(flags);

    if(t < 0) {
#if 0
        dbglog(DBG_WARNING, "pvr_wait_ready: timed out\n");
        printf("VERTBUF_ADDR: %08lx\n", PVR_GET(PVR_ISP_VERTBUF_ADDR));
        printf("TILEMAT_ADDR: %08lx\n", PVR_GET(PVR_ISP_TILEMAT_ADDR));
        printf("OPB_START: %08lx\n", PVR_GET(PVR_TA_OPB_START));
        printf("OPB_END: %08lx\n", PVR_GET(PVR_TA_OPB_END));
        printf("OPB_POS: %08lx\n", PVR_GET(PVR_TA_OPB_POS));
        printf("OPB_INIT: %08lx\n", PVR_GET(PVR_TA_OPB_INIT));
        printf("VERTBUF_START: %08lx\n", PVR_GET(PVR_TA_VERTBUF_START));
        printf("VERTBUF_END: %08lx\n", PVR_GET(PVR_TA_VERTBUF_END));
        printf("VERTBUF_POS: %08lx\n", PVR_GET(PVR_TA_VERTBUF_POS));
#endif
        return -1;
    }

    return 0;
}

int pvr_check_ready(void) {
    assert(pvr_state.valid);

    if(!pvr_state.ta_busy)
        return 0;
    else
        return -1;
}

int pvr_wait_render_done(void) {
    int t = 0;

    irq_disable_scoped();

    if(pvr_state.render_busy)
        t = genwait_wait((void *)&pvr_state.render_busy, "PVR wait render done", 100);

    return t;
}
