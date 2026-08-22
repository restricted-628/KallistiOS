/* KallistiOS ##version##

   pvr_buffers.c
   Copyright (C) 2002, 2004 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <kos/regfield.h>

#include "pvr_internal.h"
#include "pvr_multipass_layout.h"

/*

  This module handles buffer allocation for the structures that the
  TA feed into, and which the ISP/TSP read from during the scene
  rendering.

*/


/* There's quite a bit of byte vs word conversion in this file
 * these macros just help make that more readable */
#define BYTES_TO_WORDS(x) ((x) >> 2)
#define PVR_FRAME_BANK_SIZE UINT32_C(0x00400000)


/* Fill Tile Matrix buffers. This function takes a base address and sets up
   the rendering structures there. Each tile of the screen (32x32) receives
   a small buffer space. */
static void pvr_init_tile_matrix(int which, bool presort) {
    volatile pvr_ta_buffers_t   *buf;
    uint32_t      *vr;  /* Note: We're working in 4-byte pointer maths in this function */
    pvr_ta_pass_layout_t pass;
    pvr_ta_layout_t layout;
    int result;
    int i;

    vr = (uint32_t *)PVR_RAM_BASE;
    buf = pvr_state.ta_buffers + which;

    /*
        FIXME? Is this header necessary? If we're moving the tilematrix
        register to after it, how does the Dreamcast know this is here?
    */

    /* Header of zeros */
    vr += BYTES_TO_WORDS(buf->tile_matrix - PVR_REGION_HEADER_BYTES);

    for(i = 0; i < PVR_REGION_HEADER_BYTES; i += 4)
        * vr++ = 0;

    for(i = 0; i < PVR_OPB_COUNT; ++i)
        pass.opb_size[i] = pvr_state.opb_size[i];

    pass.presort = presort;

    result = pvr_ta_layout_calculate(&layout, pvr_state.tw, pvr_state.th,
                                     &pass, 1);
    assert(result == 0);

    if(result < 0)
        return;

    assert(layout.total_opb_size == buf->opb_size);
    assert(layout.region_words * sizeof(uint32_t) == buf->tile_matrix_size);
    result = pvr_ta_layout_build_regions(vr, layout.region_words, buf->opb,
                                         &layout, &pass);
    assert(result == 0);
}

/* Fill all tile matrices */
void pvr_init_tile_matrices(bool presort) {
    int i;

    for(i = 0; i < 2; i++)
        pvr_init_tile_matrix(i, presort);
}

void pvr_set_presort_mode(bool presort) {
    uint32_t tile_matrix;
    uint32_t *vr;
    int x, y;

    if(__predict_false(!pvr_state.vbuf_doublebuf))
	    pvr_wait_render_done();

    tile_matrix = pvr_state.ta_buffers[pvr_state.ta_target].tile_matrix;
    vr = (uint32_t *)PVR_RAM_BASE + BYTES_TO_WORDS(tile_matrix) + 6;

    for(x = 0; x < pvr_state.tw; x++) {
        for(y = 0; y < pvr_state.th; y++) {
            vr[0] = (y << 8) | (x << 2) | (presort << 29);
            vr += 6;
        }
    }

    vr[-6] |= BIT(31);
}


/* Allocate PVR buffers given a set of parameters

There's some confusion in here that is explained more fully in pvr_internal.h.

The other confusing thing is that texture ram is a 64-bit multiplexed space
rather than a copy of the flat 32-bit VRAM. So in order to maximize the
available texture RAM, the PVR structures for the two frames are broken
up and placed at 0x000000 and 0x400000.

*/
static int calculate_buffer_plan(const pvr_init_params_t *params,
                                 pvr_ta_pass_layout_t *pass,
                                 pvr_ta_layout_t *layout,
                                 pvr_ta_frame_layout_t *frame_layout) {
    uint32_t width;
    uint32_t height;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t bytes_per_pixel;
    uint32_t frame_size;
    int i;

    if(!params || !pass || !layout || !frame_layout || !vid_mode ||
            vid_mode->width <= 0 || vid_mode->height <= 0 ||
            params->vertex_buf_size <= 0 || params->opb_overflow_count < 0) {
        errno = EINVAL;
        return -1;
    }

    width = (uint32_t)vid_mode->width;
    height = (uint32_t)vid_mode->height;

    if(width & 31u) {
        errno = ENOTSUP;
        return -1;
    }

    tile_width = width / 32u;

    if(params->fsaa_enabled) {
        if(tile_width > UINT32_MAX / 2u) {
            errno = EOVERFLOW;
            return -1;
        }

        tile_width *= 2u;
    }

    if(height > UINT32_MAX - 31u) {
        errno = EOVERFLOW;
        return -1;
    }

    height = (height + 31u) & ~UINT32_C(31);
    tile_height = height / 32u;

    for(i = 0; i < PVR_OPB_COUNT; ++i) {
        switch(params->opb_sizes[i]) {
            case PVR_BINSIZE_0:
            case PVR_BINSIZE_8:
            case PVR_BINSIZE_16:
            case PVR_BINSIZE_32:
                pass->opb_size[i] = (uint32_t)params->opb_sizes[i] * 4u;
                break;
            default:
                errno = EINVAL;
                return -1;
        }
    }

    pass->presort = !!params->autosort_disabled;

    if(pvr_ta_layout_calculate(layout, tile_width, tile_height,
                               pass, 1) < 0)
        return -1;

    bytes_per_pixel = (uint32_t)vid_pmode_bpp[vid_mode->pm];

    if(!bytes_per_pixel || width > UINT32_MAX / height ||
            width * height > UINT32_MAX / bytes_per_pixel) {
        errno = EOVERFLOW;
        return -1;
    }

    frame_size = width * height * bytes_per_pixel;

    return pvr_ta_frame_layout_calculate(
        frame_layout, 0, PVR_FRAME_BANK_SIZE,
        (uint32_t)params->vertex_buf_size, layout->total_opb_size,
        (uint32_t)params->opb_overflow_count, layout->region_words,
        frame_size);
}

int pvr_buffers_validate(const pvr_init_params_t *params) {
    pvr_ta_pass_layout_t pass;
    pvr_ta_layout_t layout;
    pvr_ta_frame_layout_t frame_layout;

    return calculate_buffer_plan(params, &pass, &layout, &frame_layout);
}

int pvr_allocate_buffers(const pvr_init_params_t *params) {
    volatile pvr_ta_buffers_t   *buf;
    volatile pvr_frame_buffers_t    *fbuf;
    pvr_ta_pass_layout_t pass;
    pvr_ta_layout_t layout;
    pvr_ta_frame_layout_t frame_layout;
    uint32_t bank_usage;
    int i, j;

    if(calculate_buffer_plan(params, &pass, &layout, &frame_layout) < 0)
        return -1;

    /* Set screen sizes; pvr_init has ensured that we have a valid mode
       and all that by now, so we can freely dig into the vid_mode
       structure here. */
    pvr_state.w = vid_mode->width;
    pvr_state.h = (int)(layout.tile_height * 32u);
    pvr_state.tw = (int)layout.tile_width;
    pvr_state.th = (int)layout.tile_height;

    pvr_state.tsize_const = ((pvr_state.th - 1) << 16)
                            | ((pvr_state.tw - 1) << 0);

    /* Set clipping parameters */
    pvr_state.zclip = 0.0001f;
    pvr_state.pclip_left = 0;
    pvr_state.pclip_right = vid_mode->width - 1;
    pvr_state.pclip_top = 0;
    pvr_state.pclip_bottom = vid_mode->height - 1;
    pvr_state.pclip_x = (pvr_state.pclip_right << 16) | (pvr_state.pclip_left);
    pvr_state.pclip_y = (pvr_state.pclip_bottom << 16) | (pvr_state.pclip_top);
    pvr_state.next_pclip_left = pvr_state.pclip_left;
    pvr_state.next_pclip_right = pvr_state.pclip_right;
    pvr_state.next_pclip_top = pvr_state.pclip_top;
    pvr_state.next_pclip_bottom = pvr_state.pclip_bottom;
    pvr_state.next_pclip_x = pvr_state.pclip_x;
    pvr_state.next_pclip_y = pvr_state.pclip_y;
    pvr_state.curr_pclip_x = pvr_state.pclip_x;
    pvr_state.curr_pclip_y = pvr_state.pclip_y;

    /* The shared overflow area grows toward increasing addresses. */
    pvr_state.list_reg_mask = 0;

    for(i = 0; i < PVR_OPB_COUNT; i++) {
        uint32_t size_code = pass.opb_size[i] / 32u;

        pvr_state.opb_size[i] = (int)pass.opb_size[i];

        if(size_code > 0) {
            /* Convert 1, 2, and 4 units to the register's 1, 2, and 3
               encodings. */
            if(size_code == 4)
                size_code = 3;

            pvr_state.lists_enabled |= BIT(i);
            pvr_state.list_reg_mask |= size_code << (4 * i);
        }
    }

    /* Initialize each buffer set */
    for(i = 0; i < 2; i++) {
        const uint32_t bank_base = (uint32_t)i * PVR_FRAME_BANK_SIZE;

        if(pvr_ta_frame_layout_calculate(
                &frame_layout, bank_base, PVR_FRAME_BANK_SIZE,
                (uint32_t)params->vertex_buf_size, layout.total_opb_size,
                (uint32_t)params->opb_overflow_count, layout.region_words,
                frame_layout.frame_size) < 0)
            return -1;

        /* Select a pvr_buffers_t. Note that there's no good reason
           to allocate the frame buffers at the same time as the TA
           buffers except that it's handy to do it all in one place. */
        buf = pvr_state.ta_buffers + i;
        fbuf = pvr_state.frame_buffers + i;

        /* Vertex buffer */
        buf->vertex = frame_layout.vertex;
        buf->vertex_size = frame_layout.vertex_size;

        /* Object Pointer Blocks */
        buf->opb = frame_layout.opb;
        buf->opb_size = frame_layout.opb_size;

        /* Allocate extra space for overflow (when one OPB isn't big enough) */
        buf->opb_overflow_count = params->opb_overflow_count;

        /* Set up the opb pointers to each section */
        for(j = 0; j < PVR_OPB_COUNT; j++) {
            buf->opb_addresses[j] = buf->opb +
                layout.list_opb_offset[0][j];
        }

        buf->tile_matrix = frame_layout.tile_matrix;
        buf->tile_matrix_size = frame_layout.tile_matrix_size;

        /* Output buffer */
        fbuf->frame = frame_layout.frame;
        fbuf->frame_size = frame_layout.frame_size;
    }

    /* The 32-bit frame-bank usage maps to twice as much linear 64-bit VRAM. */
    bank_usage = frame_layout.bank_end - PVR_FRAME_BANK_SIZE;
    pvr_state.texture_base = bank_usage * 2u;

#if 0
    dbglog(DBG_KDEBUG, "pvr: initialized PVR buffers:\n");
    dbglog(DBG_KDEBUG, "  texture RAM begins at %08lx\n", pvr_state.texture_base);

    for(i = 0; i < 2; i++) {
        buf = pvr_state.ta_buffers + i;
        fbuf = pvr_state.frame_buffers + i;
        dbglog(DBG_KDEBUG, "  vertex/vertex_size: %08lx/%08lx\n", buf->vertex, buf->vertex_size);
        dbglog(DBG_KDEBUG, "  opb base/opb_size: %08lx/%08lx\n", buf->opb, buf->opb_size);
        dbglog(DBG_KDEBUG, "  opbs per type: %08lx %08lx %08lx %08lx %08lx\n",
               buf->opb_type[0],
               buf->opb_type[1],
               buf->opb_type[2],
               buf->opb_type[3],
               buf->opb_type[4]);
        dbglog(DBG_KDEBUG, "  tile_matrix/tile_matrix_size: %08lx/%08lx\n", buf->tile_matrix, buf->tile_matrix_size);
        dbglog(DBG_KDEBUG, "  frame/frame_size: %08lx/%08lx\n", fbuf->frame, fbuf->frame_size);
    }

    dbglog(DBG_KDEBUG, "  list_mask %08lx\n", pvr_state.list_reg_mask);
    dbglog(DBG_KDEBUG, "  w/h = %d/%d, tw/th = %d/%d\n", pvr_state.w, pvr_state.h,
           pvr_state.tw, pvr_state.th);
    dbglog(DBG_KDEBUG, "  zclip %08lx\n", *((uint32_t *)&pvr_state.zclip));
    dbglog(DBG_KDEBUG, "  pclip_left/right %08lx/%08lx\n", pvr_state.pclip_left, pvr_state.pclip_right);
    dbglog(DBG_KDEBUG, "  pclip_top/bottom %08lx/%08lx\n", pvr_state.pclip_top, pvr_state.pclip_bottom);
    dbglog(DBG_KDEBUG, "  lists_enabled %08lx\n", pvr_state.lists_enabled);
    dbglog(DBG_KDEBUG, "Free texture memory: %ld bytes\n",
           0x800000 - pvr_state.texture_base);
#endif  /* !NDEBUG */

    return 0;
}
