/* KallistiOS ##version##

   pvr_init_shutdown.c
   Copyright (C) 2002, 2004 Megan Potter
   Copyright (C) 2026 Joseph Black

 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <dc/asic.h>
#include <dc/vblank.h>
#include <kos/dbglog.h>
#include <kos/genwait.h>
#include "pvr_internal.h"

/*

   Initialization and shutdown: stuff you should only ever have to do
   once in your program.

*/

/* Simpler function which initializes the PVR using 16/16 for the opaque
   and translucent lists, and 0's for everything else; 512k of vertex
   buffer. This is equivalent to the old ta_init_defaults() for now. */
int pvr_init_defaults(void) {
    return pvr_init(&pvr_default_params);
}

static int pvr_init_common(const pvr_init_params_t *params,
                           pvr_multipass_state_t *multipass);

int pvr_init(const pvr_init_params_t *params) {
    return pvr_init_common(params, NULL);
}

int pvr_init_multipass(const pvr_init_params_t *params,
                       const pvr_pass_config_t *passes, size_t pass_count) {
    pvr_multipass_state_t *multipass;
    size_t pass;

    if(!params || !passes || !pass_count ||
            pass_count > PVR_MULTIPASS_MAX_PASSES) {
        errno = EINVAL;
        return -1;
    }

    multipass = calloc(1, sizeof(*multipass));

    if(!multipass) {
        errno = ENOMEM;
        return -1;
    }

    multipass->pass_count = pass_count;

    if(params->dma_enabled) {
        multipass->dma_buffers = calloc(2u * pass_count,
                                        sizeof(*multipass->dma_buffers));

        if(!multipass->dma_buffers) {
            free(multipass);
            errno = ENOMEM;
            return -1;
        }
    }

    for(pass = 0; pass < pass_count; ++pass) {
        int list;

        for(list = 0; list < PVR_OPB_COUNT; ++list) {
            switch(passes[pass].opb_sizes[list]) {
                case PVR_BINSIZE_0:
                case PVR_BINSIZE_8:
                case PVR_BINSIZE_16:
                case PVR_BINSIZE_32:
                    multipass->passes[pass].opb_size[list] =
                        (uint32_t)passes[pass].opb_sizes[list] * 4u;
                    break;
                default:
                    free(multipass->dma_buffers);
                    free(multipass);
                    errno = EINVAL;
                    return -1;
            }
        }

        multipass->passes[pass].presort =
            !!passes[pass].autosort_disabled;
    }

    if(pvr_init_common(params, multipass) < 0) {
        if(pvr_state.multipass == multipass)
            pvr_state.multipass = NULL;

        free(multipass->dma_buffers);
        free(multipass);
        return -1;
    }

    return 0;
}

/* Initialize the PVR chip to ready status, enabling the specified lists
   and using the specified parameters; note that bins and vertex buffers
   come from the texture memory pool! Expects that a 2D mode was
   initialized already using the vid_* API. */
static int pvr_init_common(const pvr_init_params_t *params,
                           pvr_multipass_state_t *multipass) {
    uint16_t vscale = 1024;

    /* If we're already initialized, fail */
    if(pvr_state.valid == 1) {
        dbglog(DBG_WARNING, "pvr: pvr_init called twice!\n");
        return -1;
    }

    if(!params || !vid_mode || vid_mode->width <= 0 ||
            vid_mode->height <= 0) {
        errno = EINVAL;
        return -1;
    }

    /* Check for compatibility with 3D stuff */
    if(!__is_aligned(vid_mode->width, 32)) {
        dbglog(DBG_WARNING, "pvr: mode %dx%d isn't usable for 3D (width not multiples of 32)\n",
               vid_mode->width, vid_mode->height);
        errno = ENOTSUP;
        return -1;
    }

    /* Reject an invalid or oversized memory plan before vid_empty() destroys
       the current VRAM contents or any PVR register is changed. */
    if(pvr_buffers_validate(params, multipass) < 0)
        return -1;

    /* Clear out video memory */
    vid_empty();

    /* Reset all PVR systems (in case it's still doing something) */
    PVR_SET(PVR_RESET, PVR_RESET_ALL);
    PVR_SET(PVR_RESET, PVR_RESET_NONE);

    /* Start off with a nice empty structure */
    memset((void *)&pvr_state, 0, sizeof(pvr_state));
    pvr_state.multipass = multipass;

    /* Enable DMA if the user wants that. */
    pvr_state.dma_mode = params->dma_enabled;

    /* Copy over FSAA setting. */
    pvr_state.fsaa = params->fsaa_enabled;

    pvr_state.vbuf_doublebuf = !params->vbuf_doublebuf_disabled;

    /* Everything's clear, do the initial buffer pointer setup */
    if(pvr_allocate_buffers(params, multipass) < 0)
        return -1;

    /* Initialize tile matrices */
    pvr_init_tile_matrices(!!params->autosort_disabled);

    pvr_state.list_reg_open = PVR_LIST_NONE;
    pvr_event_init();

    /* Sync all the hardware registers with our pipeline state. */
    pvr_sync_view();
    pvr_sync_reg_buffer();

    /* Clear out our stats */
    pvr_state.vbl_count = 0;
    pvr_state.frame_last_time = 0;
    pvr_state.buf_start_time = 0;
    pvr_state.reg_start_time = 0;
    pvr_state.rnd_start_time = 0;
    pvr_state.frame_last_len = -1;
    pvr_state.buf_last_len = -1;
    pvr_state.reg_last_len = -1;
    pvr_state.rnd_last_len = -1;
    pvr_state.vtx_buf_used = 0;
    pvr_state.vtx_buf_used_max = 0;

    /* If we're on a VGA box, disable vertical smoothing */
    if(vid_mode->cable_type == CT_VGA) {
        dbglog(DBG_KDEBUG, "pvr: disabling vertical scaling for VGA\n");
    } else {
        /* If using an interlaced video mode, enable vertical smoothing ("flicker filter"),
           otherwise disable it */
        if(vid_mode->flags & VID_INTERLACE) {
            dbglog(DBG_KDEBUG, "pvr: enabling vertical scaling for interlaced non-VGA\n");
            vscale++;
        } else {
            dbglog(DBG_KDEBUG, "pvr: disabling vertical scaling for progressive non-VGA\n");
        }
    }

    /* Set horizontal / vertical scale factors */
    PVR_SET(PVR_SCALER_CFG,
            FIELD_PREP(PVR_SCALER_CFG_FSAA, pvr_state.fsaa) |
            FIELD_PREP(PVR_SCALER_CFG_VSCALE_FACTOR, vscale));

    /* Hook the PVR interrupt events on G2 */
    pvr_state.vbl_handle = vblank_handler_add(pvr_vblank_handler, NULL);

    asic_evt_set_handler(ASIC_EVT_PVR_OPAQUEDONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_OPAQUEDONE, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_OPAQUEMODDONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_OPAQUEMODDONE, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_TRANSDONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_TRANSDONE, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_TRANSMODDONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_TRANSMODDONE, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_PTDONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_PTDONE, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_RENDERDONE_TSP, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_RENDERDONE_TSP, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_YUV_DONE, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_YUV_DONE, ASIC_IRQ_DEFAULT);

    /* Fault events are always enabled because the public status API latches
       them even when their debug messages are compiled out. */
    asic_evt_set_handler(ASIC_EVT_PVR_ISP_OUTOFMEM, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_ISP_OUTOFMEM, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_STRIP_HALT, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_STRIP_HALT, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_OPB_OUTOFMEM, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_OPB_OUTOFMEM, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_TA_INPUT_ERR, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_TA_INPUT_ERR, ASIC_IRQ_DEFAULT);
    asic_evt_set_handler(ASIC_EVT_PVR_TA_INPUT_OVERFLOW, pvr_int_handler, NULL);
    asic_evt_enable(ASIC_EVT_PVR_TA_INPUT_OVERFLOW, ASIC_IRQ_DEFAULT);

    /* 3d-specific parameters; these are all about rendering and
       nothing to do with setting up the video; some stuff in here
       is still unknown. */
    PVR_SET(PVR_UNK_00A8, 0x15d1c951);          /* M (Unknown magic value) */
    PVR_SET(PVR_UNK_00A0, 0x00000020);          /* M */
    /* PVR_FB_CFG_2 is configured in vid_set_mode() */
    PVR_SET(PVR_UNK_0110, 0x00093f39);          /* M */
    PVR_SET(PVR_UNK_0098, 0x00800408);          /* M */
    PVR_SET(PVR_TEXTURE_CLIP, 0x00000000);      /* texture clip distance */
    PVR_SET(PVR_SPANSORT_CFG, 0x00000101);      /* M */
    pvr_fog_table_color(0.0f, 0.5f, 0.5f, 0.5f);/* Fog table color */
    pvr_fog_vertex_color(0.5f, 0.5f, 0.5f);     /* Fog vertex color */
    PVR_SET(PVR_COLOR_CLAMP_MIN, PVR_PACK_COLOR(0, 0, 0, 0));   /* color clamp min */
    PVR_SET(PVR_COLOR_CLAMP_MAX, PVR_PACK_COLOR(1, 1, 1, 1));   /* color clamp max */
    PVR_SET(PVR_UNK_0080, 0x00000007);          /* M */
    PVR_SET(PVR_CHEAP_SHADOW, 0x00000001);      /* cheap shadow */
    PVR_SET(PVR_OBJECT_CLIP, 0x3f800000);       /* 1.0f culling threshold */
    PVR_SET(PVR_UNK_007C, 0x0027df77);          /* M */
    PVR_SET(PVR_TEXTURE_MODULO, 0x00000000);    /* stride width */
    PVR_SET(PVR_FOG_DENSITY, 0x0000ff07);       /* fog density */
    PVR_SET(PVR_UNK_0118, 0x00008040);          /* M */

    /* Initialize PVR DMA */
    sem_init((semaphore_t *)&pvr_state.dma_lock, 1);
    pvr_dma_init();

    /* Set us as valid and return success */
    pvr_state.valid = 1;
    pvr_status_advance();

    /* Validate our memory pool */
    pvr_mem_initialize((pvr_ptr_t)(PVR_RAM_INT_BASE + pvr_state.texture_base), PVR_RAM_SIZE - pvr_state.texture_base);
    pvr_mem_reset();

    return 0;
}

/* Shut down the PVR chip from ready status, leaving it in 2D mode as it
   was before the init. */
int pvr_shutdown(void) {
    pvr_multipass_state_t *multipass;

    if(!pvr_state.valid)
        return -1;

    multipass = pvr_state.multipass;

    /* Set us invalid */
    pvr_state.valid = 0;

    /* Identity-specific waits must not survive subsystem shutdown. */
    genwait_wake_all((void *)&pvr_state.queued_render_id);
    genwait_wake_all((void *)&pvr_state.registered_render_id);
    genwait_wake_all((void *)&pvr_state.render_started_id);
    genwait_wake_all((void *)&pvr_state.completed_render_id);
    genwait_wake_all((void *)&pvr_state.displayed_render_id);

    /* Stop anything that might be going on */
    PVR_SET(PVR_RESET, PVR_RESET_ALL);
    PVR_SET(PVR_RESET, PVR_RESET_NONE);

    /* Unhook any int handlers */
    vblank_handler_remove(pvr_state.vbl_handle);
    asic_evt_disable(ASIC_EVT_PVR_OPAQUEDONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_OPAQUEDONE);
    asic_evt_disable(ASIC_EVT_PVR_OPAQUEMODDONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_OPAQUEMODDONE);
    asic_evt_disable(ASIC_EVT_PVR_TRANSDONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_TRANSDONE);
    asic_evt_disable(ASIC_EVT_PVR_TRANSMODDONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_TRANSMODDONE);
    asic_evt_disable(ASIC_EVT_PVR_PTDONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_PTDONE);
    asic_evt_disable(ASIC_EVT_PVR_RENDERDONE_TSP, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_RENDERDONE_TSP);
    asic_evt_disable(ASIC_EVT_PVR_YUV_DONE, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_YUV_DONE);
    asic_evt_disable(ASIC_EVT_PVR_ISP_OUTOFMEM, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_ISP_OUTOFMEM);
    asic_evt_disable(ASIC_EVT_PVR_STRIP_HALT, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_STRIP_HALT);
    asic_evt_disable(ASIC_EVT_PVR_OPB_OUTOFMEM, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_OPB_OUTOFMEM);
    asic_evt_disable(ASIC_EVT_PVR_TA_INPUT_ERR, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_TA_INPUT_ERR);
    asic_evt_disable(ASIC_EVT_PVR_TA_INPUT_OVERFLOW, ASIC_IRQ_DEFAULT);
    asic_evt_remove_handler(ASIC_EVT_PVR_TA_INPUT_OVERFLOW);

    /* Shut down PVR DMA */
    pvr_dma_shutdown();
    pvr_txr_request_shutdown();
    pvr_event_shutdown();

    /* Invalidate our memory pool */
    pvr_mem_initialize((pvr_ptr_t)NULL, 0);
    pvr_mem_reset();

    /* Destroy the mutex */
    sem_destroy((semaphore_t *)&pvr_state.dma_lock);

    pvr_state.multipass = NULL;
    free(multipass ? multipass->dma_buffers : NULL);
    free(multipass);

    /* Clear video memory */
    vid_empty();

    /* Reset the frame buffer offset */
    vid_waitvbl();
    vid_set_start(0);

    /* Return success */
    return 0;
}
