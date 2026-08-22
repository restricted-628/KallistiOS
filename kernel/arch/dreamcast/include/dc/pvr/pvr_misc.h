/* KallistiOS ##version##

   dc/pvr/pvr_misc.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2014 Lawrence Sebald
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2024 Falco Girgis
   Copyright (C) 2026 Joseph Black
*/

/** \file       dc/pvr/pvr_misc.h
    \brief      Miscellaneous utilities for the PVR API
    \ingroup    pvr

    \author Megan Potter
    \author Roger Cattermole
    \author Paul Boese
    \author Brian Paul
    \author Lawrence Sebald
    \author Benoit Miller
    \author Ruslan Rostovtsev
    \author Falco Girgis
*/

#ifndef __DC_PVR_PVR_MISC_H
#define __DC_PVR_PVR_MISC_H

#include <stdint.h>

#include <kos/cdefs.h>
__BEGIN_DECLS


/** \brief   Pack four floating point color values into a 32-bit integer form.

    All of the color values should be between 0 and 1.

    \param  a               Alpha value
    \param  r               Red value
    \param  g               Green value
    \param  b               Blue value
    \return                 The packed color value
*/
#define PVR_PACK_COLOR(a, r, g, b) ( \
        ( ((uint8_t)( (a) * 255 ) ) << 24 ) | \
        ( ((uint8_t)( (r) * 255 ) ) << 16 ) | \
        ( ((uint8_t)( (g) * 255 ) ) <<  8 ) | \
        ( ((uint8_t)( (b) * 255 ) ) <<  0 ) )

/** \brief   Pack two floating point coordinates into one 32-bit value,
             truncating them to 16-bits each.

    \param  u               First coordinate to pack
    \param  v               Second coordinate to pack
    \return                 The packed coordinates
*/
static inline uint32_t PVR_PACK_16BIT_UV(float u, float v) {
    union {
        float f;
        uint32_t i;
    } u2, v2;

    u2.f = u;
    v2.f = v;

    return (u2.i & 0xFFFF0000) | (v2.i >> 16);
}


/** \defgroup pvr_global Global State
    \brief               PowerVR functionality which is managed globally
    \ingroup             pvr

    These are miscellaneous parameters you can set which affect the
    rendering process.
*/

/** \brief   Set the background plane color.
    \ingroup pvr_global

    This function sets the color of the area of the screen not covered by any
    other polygons.

    \param  r               Red component of the color to set
    \param  g               Green component of the color to set
    \param  b               Blue component of the color to set
*/
void pvr_set_bg_color(float r, float g, float b);

/** \brief   Set cheap shadow parameters.
    \ingroup pvr_global

    This function sets up the PVR cheap shadow parameters for use. You can only
    specify one scale value per frame, so the effect that you can get from this
    is somewhat limited, but if you want simple shadows, this is the easiest way
    to do it.

    Polygons affected by a shadow modifier volume will effectively multiply
    their final color by the scale value set here when shadows are enabled and
    the polygon is inside the modifier (or outside for exclusion volumes).

    \param  enable          Set to true to enable cheap shadow mode.
    \param  scale_value     Floating point value (between 0 and 1) representing
                            how colors of polygons affected by and inside the
                            volume will be modified by the shadow volume.
*/
void pvr_set_shadow_scale(bool enable, float scale_value);

/** \brief   Set Z clipping depth.
    \ingroup pvr_global

    This function sets the Z clipping depth. The default value for this is
    0.0001.

    \param  zc              The new value to set the z clip parameter to.
*/
void pvr_set_zclip(float zc);

/** \brief   Set the vertical scale factor.
    \ingroup pvr_global

    This function sets the vertical scale factor used when the PVR scene is
    rendered to the framebuffer. Generally you want 1.0f or near-1.0f values
    here. The default used by the PVR driver is 1.0f when using VGA, and 0.999f
    otherwise. Having a value slightly below 1.0f gives the image a pleasant
    smoothing.

    \retval 0               On success
    \retval -1              On invalid factor value
*/
int pvr_set_vertical_scale(float factor);

/** \brief   Set the translucent polygon sort mode for the next frame.
    \ingroup pvr_scene_mgmt

    This function sets the translucent polygon sort mode for the next frame of
    output, potentially switching between autosort and presort mode.

    For most programs, you'll probably want to set this at initialization time
    (with the autosort_disabled field in the pvr_init_params_t structure) and
    not mess with it per-frame. It is recommended that if you do use this
    function to change the mode that you should set it each frame to ensure that
    the mode is set properly.

    \param  presort         Set to true to set the presort mode for translucent
                            polygons, set to false to use autosort mode.
*/
void pvr_set_presort_mode(bool presort);

/** \brief   Retrieve the current VBlank count.
    \ingroup pvr_stats

    This function retrieves the number of VBlank interrupts that have occurred
    since the PVR was initialized.

    \return                 The number of VBlanks since init
*/
int pvr_get_vbl_count(void);

/** \defgroup pvr_stats         Profiling
    \brief                      Rendering stats and metrics for profiling
    \ingroup                    pvr
*/

/** \brief   PVR statistics structure.
    \ingroup pvr_stats

    This structure is used to hold various statistics about the operation of the
    PVR since initialization.
*/
typedef struct pvr_stats {
    uint64_t frame_last_time;     /**< \brief Ready-to-Ready length for the last frame in nanoseconds */
    uint64_t reg_last_time;       /**< \brief Registration time for the last frame in nanoseconds */
    uint64_t rnd_last_time;       /**< \brief Rendering time for the last frame in nanoseconds */
    uint64_t buf_last_time;       /**< \brief DMA buffer file time for the last frame in nanoseconds */
    size_t   frame_count;         /**< \brief Total number of rendered/viewed frames */
    size_t   vbl_count;           /**< \brief VBlank count */
    size_t   vtx_buffer_used;     /**< \brief Number of bytes used in the vertex buffer for the last frame */
    size_t   vtx_buffer_used_max; /**< \brief Number of bytes used in the vertex buffer for the largest frame */
    float    frame_rate;          /**< \brief Current frame rate (per second) */
    uint32_t enabled_list_mask;   /**< \brief Which lists are enabled? */
    /* ... more later as it's implemented ... */
} pvr_stats_t;

/** \brief   Get the current statistics from the PVR.
    \ingroup pvr_stats

    This function fills in the pvr_stats_t structure passed in with the current
    statistics of the system.

    \param  stat            The statistics structure to fill in. Must not be
                            NULL
    \retval 0               On success
    \retval -1              If the PVR is not initialized
*/
int pvr_get_stats(pvr_stats_t *stat);

/** \defgroup pvr_pipeline_status Pipeline Status and Faults
    \brief                         Coherent PVR pipeline state and fault records
    \ingroup                       pvr_stats

    @{
*/

/** \brief Persistent PVR fault flags.

    These flags describe faults observed since initialization or since the
    corresponding flag was cleared with pvr_clear_faults(). A fault remains
    latched after its interrupt has returned so applications can diagnose
    failures without parsing debug output.
*/
typedef enum pvr_fault {
    PVR_FAULT_NONE              = 0,
    PVR_FAULT_ISP_OUT_OF_MEMORY = 1u << 0,
    PVR_FAULT_STRIP_HALT        = 1u << 1,
    PVR_FAULT_OPB_OUT_OF_MEMORY = 1u << 2,
    PVR_FAULT_TA_INPUT_ERROR    = 1u << 3,
    PVR_FAULT_TA_INPUT_OVERFLOW = 1u << 4,
    PVR_FAULT_DMA_INCOMPLETE    = 1u << 5,
    PVR_FAULT_ALL               = (1u << 6) - 1u
} pvr_fault_t;

/** \brief Persistent details for the latest PVR fault.

    Register values are sampled in interrupt context when the fault is
    observed. They are diagnostic snapshots and must not be interpreted as
    current register state after the pipeline continues.
*/
typedef struct pvr_fault_status {
    uint32_t sequence;             /**< \brief Number of faults observed. */
    uint32_t mask;                 /**< \brief Currently latched fault flags. */
    pvr_fault_t last_fault;        /**< \brief Most recently observed fault. */
    uint32_t last_event;           /**< \brief Raw ASIC event code, if applicable. */
    uint32_t counts[6];            /**< \brief Count for fault bits 0 through 5. */
    uint32_t opb_start;            /**< \brief TA object-pointer-buffer start. */
    uint32_t opb_end;              /**< \brief TA object-pointer-buffer end. */
    uint32_t opb_position;         /**< \brief TA object-pointer-buffer position. */
    uint32_t vertex_start;         /**< \brief TA vertex-buffer start. */
    uint32_t vertex_end;           /**< \brief TA vertex-buffer end. */
    uint32_t vertex_position;      /**< \brief TA vertex-buffer position. */
} pvr_fault_status_t;

/** \brief Coherent snapshot of the software-visible PVR pipeline.

    The complete structure is copied with interrupts disabled. The sequence
    value advances on software-visible pipeline transitions; callers can use it
    to detect whether a later snapshot represents new state.
*/
typedef struct pvr_pipeline_status {
    uint32_t sequence;             /**< \brief Pipeline transition sequence. */
    uint32_t initialized;          /**< \brief Non-zero when PVR is initialized. */
    uint32_t scene_active;         /**< \brief Scene currently accepts geometry. */
    uint32_t vertex_dma_enabled;   /**< \brief Buffered vertex DMA mode enabled. */
    uint32_t dma_busy;             /**< \brief Shared PVR DMA engine is active. */
    uint32_t ta_busy;              /**< \brief TA registration is in progress. */
    uint32_t render_busy;          /**< \brief ISP/TSP rendering is in progress. */
    uint32_t display_pending;      /**< \brief Completed frame awaits display. */
    uint32_t registration_to_texture; /**< \brief TA scene targets texture RAM. */
    uint32_t render_to_texture;    /**< \brief ISP/TSP render targets texture RAM. */
    uint32_t enabled_lists;        /**< \brief Enabled pvr_list_t bit mask. */
    uint32_t transferred_lists;    /**< \brief Lists accepted by the TA. */
    uint32_t flushed_lists;        /**< \brief Current RAM-frame lists flushed early. */
    int32_t open_list;             /**< \brief Open pvr_list_t or PVR_LIST_NONE. */
    uint32_t ram_target;           /**< \brief Current RAM vertex-buffer index. */
    uint32_t ta_target;            /**< \brief Current TA buffer index. */
    uint32_t view_target;          /**< \brief Current displayed framebuffer index. */
    pvr_fault_status_t faults;     /**< \brief Persistent fault information. */
} pvr_pipeline_status_t;

/** \brief Read a coherent PVR pipeline and fault snapshot.

    \param  status          Destination structure.

    \retval 0               On success.
    \retval -1              If status is NULL or PVR is not initialized, with
                            errno set to EINVAL or ENODEV respectively.
*/
int pvr_get_pipeline_status(pvr_pipeline_status_t *status);

/** \brief Clear selected persistent PVR fault flags.

    Fault counters and the latest-fault record remain available as historical
    diagnostics. A fault arriving concurrently with this call is ordered by
    interrupt exclusion and cannot be lost.

    \param  mask            Combination of pvr_fault_t flags to clear.

    \retval 0               On success.
    \retval -1              If mask contains unknown bits or PVR is not
                            initialized, with errno set to EINVAL or ENODEV.
*/
int pvr_clear_faults(uint32_t mask);

/** @} */

/** \defgroup pvr_pipeline_events Pipeline Events
    \brief                         Optional PVR completion and fault callbacks
    \ingroup                       pvr_pipeline_status

    @{
*/

/** \brief PVR pipeline event flags. */
typedef enum pvr_event {
    PVR_EVENT_REGISTRATION_COMPLETE = 1u << 0,
    PVR_EVENT_RENDER_COMPLETE       = 1u << 1,
    PVR_EVENT_DISPLAY               = 1u << 2,
    PVR_EVENT_DMA_COMPLETE          = 1u << 3,
    PVR_EVENT_FAULT                 = 1u << 4,
    PVR_EVENT_ALL                   = (1u << 5) - 1u
} pvr_event_t;

/** \brief PVR event callback.

    The callback runs in interrupt context and must remain bounded. It must not
    allocate memory, block, start or stop the PVR, or submit rendering work. It
    may call pvr_get_pipeline_status(), clear latched fault flags, or remove an
    event handler.

    The detail value depends on event:

    - PVR_EVENT_REGISTRATION_COMPLETE: completed list mask;
    - PVR_EVENT_RENDER_COMPLETE: non-zero for a texture render target;
    - PVR_EVENT_DISPLAY: displayed framebuffer index;
    - PVR_EVENT_DMA_COMPLETE: zero on success or PVR_FAULT_DMA_INCOMPLETE;
    - PVR_EVENT_FAULT: the observed pvr_fault_t flag.

    \param  event           One pvr_event_t flag.
    \param  detail          Event-specific detail described above.
    \param  user_data       Pointer supplied when registering the handler.
*/
typedef void (*pvr_event_callback_t)(pvr_event_t event, uint32_t detail,
                                     void *user_data);

/** \brief Register an optional PVR pipeline event handler.

    Handlers run in registration order. Registration allocates one small
    bookkeeping object; no event infrastructure allocates memory or creates a
    thread merely because PVR support is initialized.

    \param  event_mask      Non-zero combination of pvr_event_t flags.
    \param  callback        Bounded interrupt-context callback.
    \param  user_data       Pointer passed to callback.

    \return                 Non-negative handler ID on success.
    \retval -1              On error, with errno set to EINVAL, ENODEV,
                            ENOMEM, EOVERFLOW, or EPERM.
*/
int pvr_event_handler_add(uint32_t event_mask,
                          pvr_event_callback_t callback, void *user_data);

/** \brief Remove a PVR pipeline event handler.

    This function is safe from within an event callback. Removal takes effect
    before a later handler dispatch when that handler has not already begun;
    memory reclamation is deferred to thread context.

    \param  handle          ID returned by pvr_event_handler_add().

    \retval 0               On success.
    \retval -1              If handle is unknown, with errno set to ENOENT.
*/
int pvr_event_handler_remove(int handle);

/** @} */

__END_DECLS

#endif /* __DC_PVR_PVR_MISC_H */
