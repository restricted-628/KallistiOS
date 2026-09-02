/* KallistiOS ##version##

   pvr_misc.c
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>

#include <arch/irq.h>
#include <kos/timer.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/regfield.h>

#include "pvr_internal.h"

/*
   These are miscellaneous parameters you can set which affect the
   rendering process.
*/

/* Set the background plane color (the area of the screen not covered by
   any other polygons) */
void pvr_set_bg_color(float r, float g, float b) {
    int ir, ig, ib;
    unsigned int i;

    ir = (int)(255 * r);
    ig = (int)(255 * g);
    ib = (int)(255 * b);

    pvr_state.bg_color = (ir << 16) | (ig << 8) | (ib << 0);

    if(pvr_state.scene_active && !pvr_state.ta_checked_ready) {
        for(i = 0; i < 3; ++i)
            pvr_state.next_background.vertices[i].color = pvr_state.bg_color;
    }
}

/* Enable/disable cheap shadow mode and set the cheap shadow scale register. */
void pvr_set_shadow_scale(bool enable, float scale_value) {
    int s = (int)(scale_value * 255);

    PVR_SET(PVR_CHEAP_SHADOW, (enable << 8) | (s & 0xFF));
}

/* Set the Z-Clip value (that is to say the depth of the background layer). */
void pvr_set_zclip(float zc) {
    pvr_state.zclip = zc;

    if(pvr_state.scene_active && !pvr_state.ta_checked_ready)
        pvr_state.next_background.depth = zc;
}

int pvr_set_culling_threshold(float threshold) {
    uint32_t encoded;
    int old_irq;

    if(!isfinite(threshold) || threshold < 0.0f) {
        errno = EINVAL;
        return -1;
    }

    memcpy(&encoded, &threshold, sizeof(encoded));
    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    /* PVR_OBJECT_CLIP consumes the IEEE-754 determinant threshold directly.
       PVR_CULLING_SMALL, CW, and CCW all consult this global value. */
    PVR_SET(PVR_OBJECT_CLIP, encoded);

    irq_restore(old_irq);
    return 0;
}

int pvr_get_culling_threshold(float *threshold) {
    uint32_t encoded;
    int old_irq;

    if(!threshold) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    encoded = PVR_GET(PVR_OBJECT_CLIP);
    irq_restore(old_irq);

    memcpy(threshold, &encoded, sizeof(encoded));
    return 0;
}

static bool clamp_endpoints_valid(uint32_t minimum, uint32_t maximum) {
    unsigned int shift;

    for(shift = 0; shift < 32; shift += 8) {
        if(((minimum >> shift) & 0xffu) > ((maximum >> shift) & 0xffu))
            return false;
    }

    return true;
}

int pvr_set_color_clamp(uint32_t minimum, uint32_t maximum) {
    int old_irq;

    if(!clamp_endpoints_valid(minimum, maximum)) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    /* Keep the endpoint pair coherent with respect to threads and PVR IRQs.
       Rendering must still be synchronized by the caller when the pair should
       take effect at a frame boundary. */
    PVR_SET(PVR_COLOR_CLAMP_MIN, minimum);
    PVR_SET(PVR_COLOR_CLAMP_MAX, maximum);

    irq_restore(old_irq);
    return 0;
}

int pvr_get_color_clamp(uint32_t *minimum, uint32_t *maximum) {
    int old_irq;

    if(!minimum || !maximum || minimum == maximum) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    *minimum = PVR_GET(PVR_COLOR_CLAMP_MIN);
    *maximum = PVR_GET(PVR_COLOR_CLAMP_MAX);

    irq_restore(old_irq);
    return 0;
}

int pvr_set_punch_through_alpha(uint32_t threshold) {
    int old_irq;

    if(threshold > UINT8_MAX) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    PVR_SET(PVR_PT_ALPHA_REF, threshold);

    irq_restore(old_irq);
    return 0;
}

int pvr_get_punch_through_alpha(uint8_t *threshold) {
    int old_irq;

    if(!threshold) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    *threshold = (uint8_t)PVR_GET(PVR_PT_ALPHA_REF);

    irq_restore(old_irq);
    return 0;
}

/* Return the current VBlank count */
int pvr_get_vbl_count(void) {
    return pvr_state.vbl_count;
}

/* Fill in a statistics structure (above) from current data. This
   is a super-set of frame count. */
int pvr_get_stats(pvr_stats_t *stat) {
    if(!pvr_state.valid)
        return -1;

    assert(stat != NULL);

    stat->enabled_list_mask = pvr_state.lists_enabled;
    stat->vbl_count = pvr_state.vbl_count;
    stat->frame_last_time = pvr_state.frame_last_len;
    stat->reg_last_time = pvr_state.reg_last_len;
    stat->rnd_last_time = pvr_state.rnd_last_len;

    if(stat->frame_last_time != 0)
        stat->frame_rate = 1000000000.0f / stat->frame_last_time;
    else
        stat->frame_rate = -1.0f;

    stat->vtx_buffer_used = pvr_state.vtx_buf_used;
    stat->vtx_buffer_used_max = pvr_state.vtx_buf_used_max;
    stat->buf_last_time = pvr_state.buf_last_len;
    stat->frame_count = pvr_state.frame_count;

    return 0;
}

void pvr_status_advance(void) {
    int old_irq = irq_disable();

    ++pvr_state.status_sequence;
    irq_restore(old_irq);
}

void pvr_fault_record(pvr_fault_t fault, uint32_t event) {
    int old_irq;
    unsigned int index;

    if(!fault || (fault & (fault - 1u)) || (fault & ~PVR_FAULT_ALL))
        return;

    index = (unsigned int)__builtin_ctz((unsigned int)fault);
    old_irq = irq_disable();

    ++pvr_state.fault_status.sequence;
    pvr_state.fault_status.mask |= fault;
    pvr_state.fault_status.last_fault = fault;
    pvr_state.fault_status.last_event = event;
    ++pvr_state.fault_status.counts[index];
    pvr_state.fault_status.opb_start = PVR_GET(PVR_TA_OPB_START);
    pvr_state.fault_status.opb_end = PVR_GET(PVR_TA_OPB_END);
    pvr_state.fault_status.opb_position = PVR_GET(PVR_TA_OPB_POS) << 2;
    pvr_state.fault_status.vertex_start = PVR_GET(PVR_TA_VERTBUF_START);
    pvr_state.fault_status.vertex_end = PVR_GET(PVR_TA_VERTBUF_END);
    pvr_state.fault_status.vertex_position = PVR_GET(PVR_TA_VERTBUF_POS);
    ++pvr_state.status_sequence;

    irq_restore(old_irq);
}

int pvr_get_pipeline_status(pvr_pipeline_status_t *status) {
    int old_irq;

    if(!status) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    status->sequence = pvr_state.status_sequence;
    status->initialized = pvr_state.valid;
    status->scene_active = pvr_state.scene_active;
    status->vertex_dma_enabled = pvr_state.dma_mode;
    status->dma_busy = !pvr_dma_ready();
    status->ta_busy = pvr_state.ta_busy;
    status->render_busy = pvr_state.render_busy;
    status->display_pending = pvr_state.render_completed;
    status->registration_to_texture = pvr_state.curr_to_texture;
    status->render_to_texture = pvr_state.was_to_texture;
    status->enabled_lists = pvr_state.lists_enabled;
    status->transferred_lists = pvr_state.lists_transferred;
    status->flushed_lists = pvr_pass_dma_buffer(
        pvr_state.ram_target,
        pvr_state.multipass ? pvr_state.multipass->build_pass : 0)->flushed;
    status->open_list = pvr_state.list_reg_open;
    status->ram_target = pvr_state.ram_target;
    status->ta_target = pvr_state.ta_target;
    status->view_target = pvr_state.view_target;
    status->scene_render_id = pvr_state.scene_render_id;
    status->queued_render_id = pvr_state.queued_render_id;
    status->registration_render_id =
        pvr_state.registration_render_id;
    status->registered_render_id = pvr_state.registered_render_id;
    status->render_started_id = pvr_state.render_started_id;
    status->active_render_id = pvr_state.active_render_id;
    status->completed_render_id = pvr_state.completed_render_id;
    status->pending_display_render_id =
        pvr_state.pending_display_render_id;
    status->displayed_render_id = pvr_state.displayed_render_id;
    memcpy(&status->faults, (const void *)&pvr_state.fault_status,
           sizeof(status->faults));

    irq_restore(old_irq);
    return 0;
}

int pvr_clear_faults(uint32_t mask) {
    int old_irq;

    if(mask & ~PVR_FAULT_ALL) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();

    if(!pvr_state.valid) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    pvr_state.fault_status.mask &= ~mask;
    ++pvr_state.status_sequence;

    irq_restore(old_irq);
    return 0;
}

int pvr_vertex_dma_enabled(void) {
    return pvr_state.dma_mode;
}

/******** INTERNAL STUFF ************************************************/

/* Update statistical counters */
void pvr_sync_stats(int event) {
    uint64_t t;
    volatile pvr_ta_buffers_t *buf;

    if(event == PVR_SYNC_VBLANK) {
        pvr_state.vbl_count++;
    }
    else {
        /* Get the current time */
        t = timer_ns_gettime64();

        switch(event) {
            case PVR_SYNC_REGSTART:
                pvr_state.reg_start_time = t;
                break;

            case PVR_SYNC_REGDONE:
                pvr_state.reg_last_len = t - pvr_state.reg_start_time;

                buf = pvr_state.ta_buffers + pvr_state.ta_target;
                pvr_state.vtx_buf_used = PVR_GET(PVR_TA_VERTBUF_POS) - buf->vertex;

                if(pvr_state.vtx_buf_used > pvr_state.vtx_buf_used_max)
                    pvr_state.vtx_buf_used_max = pvr_state.vtx_buf_used;

                break;

            case PVR_SYNC_RNDSTART:
                pvr_state.rnd_start_time = t;
                break;

            case PVR_SYNC_RNDDONE:
                pvr_state.rnd_last_len = t - pvr_state.rnd_start_time;
                break;

            case PVR_SYNC_BUFSTART:
                pvr_state.buf_start_time = t;
                break;

            case PVR_SYNC_BUFDONE:
                pvr_state.buf_last_len = t - pvr_state.buf_start_time;
                break;

            case PVR_SYNC_PAGEFLIP:
                pvr_state.frame_last_len = t - pvr_state.frame_last_time;
                pvr_state.frame_last_time = t;
                pvr_state.frame_count++;
                break;
        }
    }
}

/* Synchronize the viewed page with what's in pvr_state */
void pvr_sync_view(void) {
    vid_set_start(pvr_state.frame_buffers[pvr_state.view_target].frame);
}

/* Synchronize the registration buffer with what's in pvr_state */
void pvr_sync_reg_buffer(void) {
    volatile pvr_ta_buffers_t *buf;
    uint32_t opb_start;

    buf = pvr_state.ta_buffers + pvr_state.ta_target;

    /* A new TA bank always begins at pass zero. Later passes preserve the
       shared parameter and overflow cursors through PVR_TA_LIST_CONT. */
    if(pvr_state.multipass) {
        pvr_state.multipass->ta_pass = 0;
        pvr_activate_pass(0);
        opb_start = buf->opb +
            pvr_state.multipass->layout.pass_opb_offset[0];
    }
    else {
        opb_start = buf->opb;
    }

    /* Reset TA */
    //PVR_SET(PVR_RESET, PVR_RESET_TA);
    //PVR_SET(PVR_RESET, PVR_RESET_NONE);

    /* Set buffer pointers */
    PVR_SET(PVR_TA_OPB_START,       opb_start);
    PVR_SET(PVR_TA_OPB_INIT,        buf->opb + buf->opb_size);
    PVR_SET(PVR_TA_OPB_END,         buf->opb + buf->opb_size * (1 + buf->opb_overflow_count));
    PVR_SET(PVR_TA_VERTBUF_START,   buf->vertex);
    PVR_SET(PVR_TA_VERTBUF_END,     buf->vertex + buf->vertex_size);

    /* Misc config parameters */
    PVR_SET(PVR_TILEMAT_CFG,        pvr_state.tsize_const);     /* Tile count: (H/32-1) << 16 | (W/32-1) */
    PVR_SET(PVR_OPB_CFG,            pvr_state.list_reg_mask);   /* List enables */
    PVR_SET(PVR_TA_INIT,            PVR_TA_INIT_GO);            /* Confirm settings */
    (void)PVR_GET(PVR_TA_INIT);

#if 0
    printf("== SYNC REG BUFFER:\n");
    printf("TA_OL_BASE: %08lx\nTA_OL_LIMIT: %08lx\nTA_NEXT_OPB: %08lx\n",
           PVR_GET(TA_OL_BASE), PVR_GET(TA_OL_LIMIT), PVR_GET(TA_NEXT_OPB) << 2);
#endif
}

void pvr_continue_ta_pass(size_t next_pass) {
    pvr_multipass_state_t *multipass = pvr_state.multipass;
    volatile pvr_ta_buffers_t *buffer;

    assert(multipass && next_pass < multipass->pass_count);

    multipass->ta_pass = next_pass;
    pvr_activate_pass(next_pass);
    pvr_state.lists_transferred = 0;
    pvr_state.lists_closed = 0;
    pvr_state.list_reg_open = PVR_LIST_NONE;

    /* Continuation preserves the shared parameter write position and overflow
       cursor. Only the next pass's initial OPB and allocation sizes change. */
    buffer = pvr_state.ta_buffers + pvr_state.ta_target;
    PVR_SET(PVR_TA_OPB_START, buffer->opb +
            multipass->layout.pass_opb_offset[next_pass]);
    PVR_SET(PVR_OPB_CFG, multipass->list_reg_mask[next_pass]);
    PVR_SET(PVR_TA_LIST_CONT, BIT(31));
    (void)PVR_GET(PVR_TA_LIST_CONT);
    pvr_status_advance();
}

/* Begin a render operation that has been queued completely (i.e., the
   opposite of ta_target) */
void pvr_begin_queued_render(void) {
    volatile pvr_ta_buffers_t   *tbuf;
    volatile pvr_frame_buffers_t    *rbuf;
    pvr_bkg_poly_t  *bkg;
    uint32_t      vert_end;
    int bufn = pvr_state.view_target;
    union {
        float    f;
        uint32_t i;
    } zclip;

    /* Get the appropriate buffer */
    tbuf = pvr_state.ta_buffers + (pvr_state.ta_target ^ pvr_state.vbuf_doublebuf);
    rbuf = pvr_state.frame_buffers + (bufn ^ 1);

    /* Calculate background value for below */
    /* Small side note: during setup, the value is originally
       0x01203000... I'm thinking that the upper word signifies
       the length of the background plane list in dwords
       shifted up by 4. */
    vert_end = 0x01000000 | ((PVR_GET(PVR_TA_VERTBUF_POS) - tbuf->vertex) << 1);

    /* Throw the background data on the end of the TA's list */
    bkg = (pvr_bkg_poly_t *)(PVR_RAM_BASE | PVR_GET(PVR_TA_VERTBUF_POS));

    *bkg = (pvr_bkg_poly_t){
        .flags1 = 0x90800000,    /* These are from libdream.. ought to figure out */
        .flags2 = 0x20800440,    /*   what they mean for sure... heh =) */
        .dummy  = 0,
        .x1     = pvr_state.curr_background.vertices[0].x,
        .y1     = pvr_state.curr_background.vertices[0].y,
        .z1     = pvr_state.curr_background.vertices[0].z,
        .argb1  = pvr_state.curr_background.vertices[0].color,
        .x2     = pvr_state.curr_background.vertices[1].x,
        .y2     = pvr_state.curr_background.vertices[1].y,
        .z2     = pvr_state.curr_background.vertices[1].z,
        .argb2  = pvr_state.curr_background.vertices[1].color,
        .x3     = pvr_state.curr_background.vertices[2].x,
        .y3     = pvr_state.curr_background.vertices[2].y,
        .z3     = pvr_state.curr_background.vertices[2].z,
        .argb3  = pvr_state.curr_background.vertices[2].color,
    };

    /* Reset the ISP/TSP, just in case */
    //PVR_SET(PVR_RESET, PVR_RESET_ISPTSP);
    //PVR_SET(PVR_RESET, PVR_RESET_NONE);

    /* Finish up rendering the current frame (into the other buffer) */
    PVR_SET(PVR_ISP_TILEMAT_ADDR, tbuf->tile_matrix);
    PVR_SET(PVR_ISP_VERTBUF_ADDR, tbuf->vertex);

    if(!pvr_state.curr_to_texture)
        PVR_SET(PVR_RENDER_ADDR, rbuf->frame);
    else {
        PVR_SET(PVR_RENDER_ADDR, pvr_state.to_txr_addr | BIT(24));
        PVR_SET(PVR_RENDER_ADDR_2, pvr_state.to_txr_addr | BIT(24));
    }

    PVR_SET(PVR_BGPLANE_CFG, vert_end); /* Bkg plane location */
    zclip.f = pvr_state.curr_background.depth;
    PVR_SET(PVR_BGPLANE_Z, zclip.i);
    PVR_SET(PVR_PCLIP_X, pvr_state.curr_pclip_x);
    PVR_SET(PVR_PCLIP_Y, pvr_state.curr_pclip_y);

    if(!pvr_state.curr_to_texture)
        PVR_SET(PVR_RENDER_MODULO, (pvr_state.w * vid_pmode_bpp[vid_mode->pm]) / 8);
    else
        PVR_SET(PVR_RENDER_MODULO, pvr_state.to_txr_rp);

    // XXX Do we _really_ need this every time?
    // SETREG(PVR_FB_CFG_2, 0x00000009);        /* Alpha mode */
    PVR_SET(PVR_ISP_START, PVR_ISP_START_GO);   /* Start render */
}

void pvr_blank_polyhdr(int type) {
    pvr_poly_hdr_t poly;

    // Make it.
    pvr_blank_polyhdr_buf(type, &poly);

    // Submit it
    pvr_prim(&poly, sizeof(poly));
}

void pvr_blank_polyhdr_buf(int type, pvr_poly_hdr_t *poly) {
    /* Empty it out */
    memset(poly, 0, sizeof(pvr_poly_hdr_t));

    /* Put in the list type */
    poly->cmd = FIELD_PREP(PVR_TA_CMD_TYPE, type) | 0x80840012;
}

static pvr_ptr_t pvr_get_frame_buffer(bool back) {
    unsigned int idx;
    uint32_t addr;

    irq_disable_scoped();

    /* The front buffer may not have been fully rendered or submitted to the
       video hardware yet. In case this has yet to happen, we want the second
       view target, aka. the one not currently being displayed. */
    idx = pvr_state.view_target ^ (pvr_state.render_busy | pvr_state.render_completed) ^ back;

    addr = pvr_state.frame_buffers[idx].frame & (PVR_RAM_SIZE - 1);

    /* The front buffer is in 32-bit memory, convert its address to make it
       addressable from the 64-bit memory */
    return (pvr_ptr_t)(addr * 2 + PVR_RAM_BASE);
}

pvr_ptr_t pvr_get_front_buffer(void) {
    return pvr_get_frame_buffer(false);
}

pvr_ptr_t pvr_get_back_buffer(void) {
    return pvr_get_frame_buffer(true);
}

int pvr_set_vertical_scale(float factor) {
    uint32_t f16;
    uint32_t cfg;

    if(factor == 0.0f)
        return -1;

    f16 = 1024.0f / factor;

    if(f16 == 0 || f16 >= 65536)
        return -1;

    irq_disable_scoped();

    cfg = PVR_GET(PVR_SCALER_CFG);

    cfg &= ~PVR_SCALER_CFG_VSCALE_FACTOR;
    cfg |= FIELD_PREP(PVR_SCALER_CFG_VSCALE_FACTOR, f16);

    PVR_SET(PVR_SCALER_CFG, cfg);

    return 0;
}
