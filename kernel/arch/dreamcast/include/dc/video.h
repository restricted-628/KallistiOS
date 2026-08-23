/* KallistiOS ##version##

   dc/video.h

   Copyright (C) 2001 Anders Clerwall (scav)
   Copyright (C) 2023-2024 Donald Haase
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/video.h
    \brief   Functions related to video output.
    \ingroup video_display

    This file deals with the video output hardware in the Dreamcast. There are
    functions defined herein that deal with setting up the video hardware,
    defining the resolution of the display, dealing with the framebuffer, etc.

    \author Anders Clerwall
    \author Megan Potter
*/

#ifndef __DC_VIDEO_H
#define __DC_VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include <kos/cdefs.h>
__BEGIN_DECLS

/** \defgroup video_display Display
    \brief                  Display and framebuffer configuration
    \ingroup                video
*/

/** \defgroup video_modes Modes
    \brief                Video display modes and management
    \ingroup              video_display
*/

/** \defgroup video_cables Cables
    \brief                 Cable/connector types and management for display
    \ingroup               video_modes
*/

/** \defgroup vid_ctype Types
    \brief              Type of cable connected to the DC
    \ingroup            video_cables

    The vid_check_cable() function will return one of this set of values to let
    you know what type of cable is connected to the Dreamcast. These are also
    used in the video mode settings to limit modes to certain cable types.

    @{
*/
#define CT_ANY          -1  /**< \brief Any cable type. Used only internally. */
#define CT_VGA          0   /**< \brief VGA Box */
#define CT_NONE         1   /**< \brief Nothing connected */
#define CT_RGB          2   /**< \brief RGB/SCART cable */
#define CT_COMPOSITE    3   /**< \brief Composite cable or RF switch */
/** @} */

/** \defgroup video_modes_pixel Pixel Modes
    \brief                      Pixel mode settings for the framebuffer
    \ingroup                    video_modes
*/

/** \brief              Pixel mode values for the framebuffer

    This set of constants control the pixel mode that the framebuffer is set to.
    These are hardware-based values and get set in bits 2 and 3 of PVR_FB_CFG_1.
*/
typedef enum vid_pixel_mode {
  PM_RGB555   = 0,       /**< \brief RGB555 pixel mode (15-bit) */
  PM_RGB565   = 1,       /**< \brief RGB565 pixel mode (16-bit) */
  PM_RGB888P  = 2,       /**< \brief RBG888 packed pixel mode (24-bit) */
  PM_RGB0888  = 3,       /**< \brief RGB0888 pixel mode (32-bit) */
  PM_RGB888   = 3        /**< \brief Backwards compatibility support */
} vid_pixel_mode_t;

/** \brief   Video pixel mode depths
    \ingroup video_modes_pixel
*/
static const uint8_t vid_pmode_bpp[4] = {2, 2, 3, 4};

/** \defgroup video_modes_display   Types
    \brief                          Display mode type values
    \ingroup                        video_modes
*/

/** \brief   Generic Display Modes
    \ingroup video_modes_display
 */
typedef enum vid_display_mode_generic {
    DM_GENERIC_FIRST = 0x1000,      /**< \brief First valid generic mode */
    DM_320x240 = 0x1000,            /**< \brief 320x240 resolution */
    DM_640x480,                     /**< \brief 640x480 resolution */
    DM_256x256,                     /**< \brief 256x256 resolution */
    DM_768x480,                     /**< \brief 768x480 resolution */
    DM_768x576,                     /**< \brief 768x576 resolution */
    DM_GENERIC_LAST = DM_768x576    /**< \brief Last valid generic mode */
} vid_display_mode_generic_t;

/** \brief   Multi-buffered mode setting.
    \ingroup video_modes_display

    OR this with the generic mode to get multiple framebuffers instead of one.
*/
#define DM_MULTIBUFFER  0x2000

/** \brief  Default video mode. */
#define DEFAULT_VID_MODE    DM_640x480

/** \brief  Default pixel mode for video. */
#define DEFAULT_PIXEL_MODE  PM_RGB565

/* ------------------------------------------------------------------------- */
/* More specific modes (and actual indices into the mode table) */

/** \brief   Specific Display Modes
    \ingroup video_modes_display
*/
typedef enum vid_display_mode {
    DM_INVALID = 0,                 /**< \brief Invalid display mode */
    /* Valid modes below */
    DM_320x240_VGA = 1,             /**< \brief 320x240 VGA 60Hz */
    DM_320x240_NTSC,                /**< \brief 320x240 NTSC 60Hz */
    DM_640x480_VGA,                 /**< \brief 640x480 VGA 60Hz */
    DM_640x480_NTSC_IL,             /**< \brief 640x480 NTSC Interlaced 60Hz */
    DM_640x480_PAL_IL,              /**< \brief 640x480 PAL Interlaced 50Hz */
    DM_256x256_PAL_IL,              /**< \brief 256x256 PAL Interlaced 50Hz */
    DM_768x480_NTSC_IL,             /**< \brief 768x480 NTSC Interlaced 60Hz */
    DM_768x576_PAL_IL,              /**< \brief 768x576 PAL Interlaced 50Hz */
    DM_768x480_PAL_IL,              /**< \brief 768x480 PAL Interlaced 50Hz */
    DM_320x240_PAL,                 /**< \brief 320x240 PAL 50Hz */
    /* The below is only for counting.. */
    DM_SENTINEL,                    /**< \brief Sentinel value, for counting */
    DM_MODE_COUNT                   /**< \brief Number of modes */
} vid_display_mode_t;

/** \brief Refresh-standard preference for generic mode resolution. */
typedef enum vid_mode_standard {
    /** Preserve the established KOS table-order preference. */
    VID_MODE_STANDARD_DEFAULT = 0,
    /** Select a 60 Hz non-PAL mode. */
    VID_MODE_STANDARD_60HZ,
    /** Select a 50 Hz PAL mode. */
    VID_MODE_STANDARD_50HZ
} vid_mode_standard_t;

/** \defgroup vid_flags Flags
    \brief              vid_mode_t Field Flags
    \ingroup            video_modes

    These flags indicate various things related to the modes for a vid_mode_t.

    @{
*/
#define VID_INTERLACE   0x00000001  /**< \brief Interlaced display */
#define VID_LINEDOUBLE  0x00000002  /**< \brief Display each scanline twice */
#define VID_PIXELDOUBLE 0x00000004  /**< \brief Display each pixel twice */
#define VID_PAL         0x00000008  /**< \brief 50Hz refresh rate, if not VGA */
/** @} */

/** \brief   Video mode structure.
    \ingroup video_modes

    KOS maintains a list of valid video modes internally that correspond to the
    specific display modes enumeration. Each of them is built of one of these.

    \headerfile dc/video.h
*/
typedef struct vid_mode {
    uint16_t  generic;    /**< \brief Generic mode type for vid_set_mode() */
    uint16_t  width;      /**< \brief Width of the display, in pixels */
    uint16_t  height;     /**< \brief Height of the display, in pixels */
    uint32_t  flags;      /**< \brief Combination of one or more VID_* flags */

    int8_t    cable_type; /**< \brief Allowed cable type */
    vid_pixel_mode_t  pm; /**< \brief Pixel mode */

    uint16_t  scanlines;  /**< \brief Number of scanlines */
    uint16_t  clocks;     /**< \brief Clocks per scanline */
    uint16_t  bitmapx;    /**< \brief Bitmap window X position */
    uint16_t  bitmapy;    /**< \brief Bitmap window Y position (automatically
                                    increased for PAL) */
    uint16_t  scanint1;   /**< \brief First scanline interrupt position */
    uint16_t  scanint2;   /**< \brief Second scanline interrupt position
                                    (automatically doubled for VGA) */
    uint16_t  borderx1;   /**< \brief Border X starting position */
    uint16_t  borderx2;   /**< \brief Border X stop position */
    uint16_t  bordery1;   /**< \brief Border Y starting position */
    uint16_t  bordery2;   /**< \brief Border Y stop position */

    uint16_t  fb_curr;    /**< \brief Current framebuffer */
    uint16_t  fb_count;   /**< \brief Number of framebuffers */
    size_t  fb_size;      /**< \brief Size of each framebuffer */
} vid_mode_t;

/** \brief Coherent snapshot of the physical display scanout state.
    \ingroup video_display

    The scanline is the raw hardware timing counter. It is not a framebuffer Y
    coordinate: blanking, line doubling, interlace, and custom timing modes can
    all make those values differ.
*/
typedef struct vid_scanout_status {
    uint16_t scanline;    /**< \brief Physical scanline counter (0-1023). */
    bool field;           /**< \brief Current interlaced field selector. */
    bool blank;           /**< \brief Horizontal or vertical blanking active. */
    bool hsync;           /**< \brief Horizontal synchronization active. */
    bool vsync;           /**< \brief Vertical synchronization active. */
} vid_scanout_status_t;

/** \brief Framebuffer output-filter state.
    \ingroup video_display

    vertical_scale is the exact 16-bit hardware coefficient. The effective
    scale factor is 1024 / vertical_scale. Exposing the coefficient avoids
    assigning misleading names to values whose correct choice depends on the
    complete display timing.
*/
typedef struct vid_display_filter {
    bool dithering;          /**< \brief Framebuffer dithering enabled. */
    bool antialiasing;       /**< \brief Horizontal scaler/antialiasing enabled. */
    uint16_t vertical_scale; /**< \brief Vertical-scale coefficient. */
} vid_display_filter_t;

/** \brief   The list of builtin video modes. Do not modify these!
    \ingroup video_modes
*/
extern vid_mode_t vid_builtin[DM_MODE_COUNT];

/** \brief   The current video mode. Do not modify directly!
    \ingroup video_modes
*/
extern vid_mode_t *vid_mode;

/*  These point to the current drawing area. If you're not using a multi-buffered
    mode, that means they do what KOS always used to do (they'll point at the
    start of VRAM). If you're using a multi-buffered mode, they'll point at the
    next framebuffer to be displayed. You must use vid_flip for this to work
    though (if you use vid_set_start, they'll point at the display base, for
    compatibility's sake).
*/

/** \defgroup video_fb Framebuffer
    \brief             API for framebuffer management
    \ingroup           video_display
*/

/** \brief   16-bit size pointer to the current drawing area.
    \ingroup video_fb
*/
extern uint16_t *vram_s;

/** \brief   32-bit size pointer to the current drawing area.
    \ingroup video_fb
*/
extern uint32_t *vram_l;


/** \brief   Retrieve the connected video cable type.
    \ingroup video_cables

    This function checks the video cable and reports what it finds.

    \retval CT_VGA          If a VGA Box or cable is connected.
    \retval CT_NONE         If nothing is connected.
    \retval CT_RGB          If a RGB/SCART cable is connected.
    \retval CT_COMPOSITE    If a composite cable or RF switch is connected.
*/
int8_t vid_check_cable(void);

/** \brief Resolve a display request without programming video hardware.

    Specific DM_* indices resolve directly. Generic modes are selected for the
    supplied cable and refresh-standard preference. The multibuffer flag is
    honored and the resulting framebuffer count is bounded by available VRAM.

    \param dm              Specific or generic display mode, optionally ORed
                            with DM_MULTIBUFFER.
    \param pm              Pixel mode.
    \param cable_type      One of the connected CT_* values.
    \param standard        Generic-mode refresh preference. VGA resolution is
                            always 60 Hz regardless of this preference.
    \param mode            Output resolved mode.

    \retval 0              On success.
    \retval -1             On failure with errno set.
*/
int vid_mode_resolve(int dm, vid_pixel_mode_t pm, int8_t cable_type,
                     vid_mode_standard_t standard, vid_mode_t *mode);

/** \brief Validate a complete mode against a cable and available VRAM.

    This function does not program hardware. A zero fb_size is accepted and
    means that the checked setter should derive the tightly packed size.

    \param mode            Mode to validate.
    \param cable_type      Connected CT_* value or CT_ANY to skip the
                            connection check.

    \retval 0              On success.
    \retval -1             On failure with errno set.
*/
int vid_mode_validate(const vid_mode_t *mode, int8_t cable_type);

/** \brief Copy the current mode into caller-owned storage.

    \param mode            Output mode snapshot.

    \retval 0              On success.
    \retval -1             If video is not initialized or mode is NULL.
*/
int vid_get_mode(vid_mode_t *mode);

/** \brief Read a coherent snapshot of the physical display scanout state.
    \ingroup video_display

    The underlying register is sampled once, so every returned field describes
    the same instant. This function allocates no memory and is IRQ-safe.

    \param status          Output scanout snapshot.

    \retval 0              On success.
    \retval -1             If \p status is NULL, with errno set to EFAULT.
*/
int vid_get_scanout_status(vid_scanout_status_t *status);

/** \brief Read the current framebuffer output-filter state.
    \ingroup video_display

    \param filter          Output filter snapshot.

    \retval 0              On success.
    \retval -1             If \p filter is NULL, with errno set to EFAULT.
*/
int vid_get_display_filter(vid_display_filter_t *filter);

/** \brief Set framebuffer output filters without disturbing other fields.
    \ingroup video_display

    The framebuffer and scaler control registers are updated with
    read-modify-write operations while interrupts are disabled. Alpha-related
    framebuffer configuration and unrelated scaler fields are preserved.

    \param filter          Requested filter state. vertical_scale must be
                            nonzero.

    \retval 0              On success.
    \retval -1             On invalid input, with errno set.
*/
int vid_set_display_filter(const vid_display_filter_t *filter);

/** \brief   Set the VRAM convenience pointers.
    \ingroup video_fb

    This function sets the vram_s and vram_l pointers to specified offset
    within VRAM. In multibuffered mode it allows manual management of them.

    \param  base            The offset within VRAM to set the base to.
*/
void vid_set_vram(uint32_t base);

/** \brief   Set the VRAM base of the framebuffer.
    \ingroup video_fb

    This function sets the vram_s and vram_l pointers to specified offset
    within VRAM and sets the start position of the framebuffer to the same
    offset.

    \param  base            The offset within VRAM to set the base to.
*/
void vid_set_start(uint32_t base);

/** \brief   Get the VRAM base of a framebuffer.
    \ingroup video_fb

    This function gets the position of the specified framebuffer within VRAM.
    Any invalid fb value will be treated as the current framebuffer.

    \param  fb            The number of the framebuffer or -1 for current.
*/
uint32_t vid_get_start(int32_t fb);

/** \brief   Set the current framebuffer in a multibuffered setup.
    \ingroup video_fb

    This function sets the displayed framebuffer to the specified buffer.
    Unlike vid_set_fb, this does not point the vram pointers to the next
    framebuffer, allowing for non-linear management of the FBs.

    \param  fb              The framebuffer to display or any other value
                                to display the next one.

*/
void vid_set_fb(int32_t fb);

/** \brief   Flip to a framebuffer in a multibuffered setup.
    \ingroup video_fb

    This function sets the displayed framebuffer to the specified buffer and
    sets the vram_s and vram_l pointers to point at the next framebuffer, to
    allow for tearing-free framebuffer-direct drawing.

    \param  fb              The framebuffer to display or any other value
                                to display the next one.

*/
void vid_flip(int32_t fb);

/** \brief   Set the border color of the display.
    \ingroup video_display

    This sets the color of the border area of the display.

    \note
    On some screens, the border area may not be shown at all,
    whereas on some displays you may see the whole thing.

    \param  r               The red value of the color (0-255).
    \param  g               The green value of the color (0-255).
    \param  b               The blue value of the color (0-255).

    \return                 Old border color value (RGB888)
*/
uint32_t vid_border_color(uint8_t r, uint8_t g, uint8_t b);

/** \brief   Clear the framebuffer.
    \ingroup video_fb

    This function sets the whole framebuffer to the specified color. Internally,
    this uses the store queues to actually clear the display entirely.

    \note
    This operates via the vram convenience pointers. In multibuffered mode,
    by default it will clear the framebuffer you are currently writing to
    rather than the one being displayed.

    \param  r               The red value of the color (0-255).
    \param  g               The green value of the color (0-255).
    \param  b               The blue value of the color (0-255).
*/
void vid_clear(uint8_t r, uint8_t g, uint8_t b);

/** \brief   Clear VRAM.
    \ingroup video_fb

    This function is essentially a memset() for the whole of VRAM that will
    clear it all to 0 bytes.
*/
void vid_empty(void);

/** \brief   Get the state of video output.
    \ingroup video_display

    This function gets the state of video output as set via vid_set_enabled.

    \return                 true if enabled, false if not.
*/
bool vid_get_enabled(void);

/** \brief   Enable/disable the display.
    \ingroup video_display

    This function enables or disables video output

    \param  val             true to enable video output, false to disable.

    \note
    Unlike vid_clear/vid_empty this does not modify any framebuffer.
    Instead it merely sets registers that immediately disable output.
*/
void vid_set_enabled(bool val);

/** \defgroup video_misc Miscellaneous
    \brief               Miscellaneous video API utilities
    \ingroup             video
*/

/** \brief   Wait for VBlank.
    \ingroup video_misc

    This function busy loops until the vertical blanking period starts.
*/
void vid_waitvbl(void);

/** \brief   Set the video mode.
    \ingroup video_modes

    This function sets the current video mode to the one specified by the
    parameters.

    \param  dm              The display mode to use. One of the DM_* values.
    \param  pm              The pixel mode to use. One of the PM_* values.
*/
void vid_set_mode(int dm, vid_pixel_mode_t pm);

/** \brief Set a built-in or generic video mode with status reporting.

    This checked counterpart preserves vid_set_mode()'s established generic
    table-order policy. Use vid_mode_resolve() followed by
    vid_set_mode_ex_checked() for an explicit 50/60-Hz preference.

    \return                 0 on success, or -1 with errno set.
*/
int vid_set_mode_checked(int dm, vid_pixel_mode_t pm);

/** \brief Set a mode with an explicit generic refresh-standard preference.

    Specific DM_* indices remain explicit and ignore \p standard. Generic
    modes select a compatible 50 or 60 Hz timing before any register changes.

    \return                 0 on success, or -1 with errno set.
*/
int vid_set_mode_standard_checked(int dm, vid_pixel_mode_t pm,
                                  vid_mode_standard_t standard);

/** \brief   Set the video mode.
    \ingroup video_modes

    This function sets the current video mode to the mode structure passed in.
    You can use this to add support to your program for modes that KOS doesn't
    have support for built-in (of course, you should tell us the settings so we
    can add them into KOS if you do this).

    \param  mode            A filled in vid_mode_t for the mode wanted.
*/
void vid_set_mode_ex(vid_mode_t *mode);

/** \brief Validate and set a caller-defined mode without modifying it.

    fb_size is derived when zero. All geometry, pixel-mode, cable, and VRAM
    capacity checks complete before the display is blanked or registers change.

    \return                 0 on success, or -1 with errno set.
*/
int vid_set_mode_ex_checked(const vid_mode_t *mode);

/** \defgroup video_init Initialization
    \brief               Initialization and shutdown of the video subsystem
    \ingroup             video

    @{
*/

/** \brief   Initialize the video system.

    This function initializes the video display, setting the mode to the
    specified parameters, clearing vram, and setting the first framebuffer as
    active.

    \param  disp_mode       The display mode to use. One of the DM_* values.
    \param  pixel_mode      The pixel mode to use. One of the PM_* values.
*/
void vid_init(int disp_mode, vid_pixel_mode_t pixel_mode);

/** \brief Initialize video with status reporting.

    VRAM is cleared only after the requested mode has been installed.

    \return                 0 on success, or -1 with errno set.
*/
int vid_init_checked(int disp_mode, vid_pixel_mode_t pixel_mode);

/** \brief   Shut down the video system.

    This function reinitializes the video system to what dcload and friends
    expect it to be.
*/
void vid_shutdown(void);

/** @} */

/** \brief   Take a screenshot.
    \ingroup video_fb

    This function takes the current framebuffer (/vram_l) and dumps it out
    to a PPM file.

    \param  destfn          The filename to save to.
    \return                 0 on success, <0 on failure.
*/
int vid_screen_shot(const char *destfn);

/** \brief   Create image data from the current framebuffer.
    \ingroup video_fb

    This function takes the current framebuffer (/vram_l) and converts it into
    24bpp image data. The function allocates memory for the image data, which
    the caller is responsible for freeing.

    \param  buffer          A pointer to a uint8_t* that will be allocated and
                            filled with the image data.
    \return                 The size of the created image data in bytes, or 0 on failure.
*/
size_t vid_screen_shot_data(uint8_t **buffer);

/** \brief   Enable or disable dithering.
    \ingroup video_fb

    This compatibility setter preserves every unrelated framebuffer-control
    field. New code that needs checked updates or coherent filter queries can
    use vid_set_display_filter() and vid_get_display_filter().

    \param  enable          Whether or not dithering should be enabled.
*/
void vid_set_dithering(bool enable);

__END_DECLS

#endif  /* __DC_VIDEO_H */
