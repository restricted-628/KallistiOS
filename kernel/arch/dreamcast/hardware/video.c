/* KallistiOS ##version##

   video.c

   Copyright (C) 2001 Anders Clerwall (scav)
   Copyright (C) 2000-2001 Megan Potter
   Copyright (C) 2023-2024 Donald Haase
   Copyright (C) 2026 Joseph Black
 */

#include <dc/video.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <kos/dbglog.h>
#include <kos/irq.h>
#include <kos/platform.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "video_mode_internal.h"
#include "video_raster_internal.h"

/*-----------------------------------------------------------------------------*/
/* This table is indexed w/ DM_* */
vid_mode_t vid_builtin[DM_MODE_COUNT] = {
    /* NULL mode.. */
    /* DM_INVALID = 0 */
    { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 },

    /* 320x240 VGA 60Hz */
    /* DM_320x240_VGA */
    {
        DM_320x240,
        320, 240,
        VID_PIXELDOUBLE | VID_LINEDOUBLE,
        CT_VGA,
        0,
        262, 857,
        172, 40,
        21, 260,
        141, 843,
        24, 263,
        0, 1, 0
    },

    /* 320x240 NTSC 60Hz */
    /* DM_320x240_NTSC */
    {
        DM_320x240,
        320, 240,
        VID_PIXELDOUBLE | VID_LINEDOUBLE,
        CT_ANY,
        0,
        262, 857,
        164, 24,
        21, 260,
        141, 843,
        24, 263,
        0, 1, 0
    },

    /* 640x480 VGA 60Hz */
    /* DM_640x480_VGA */
    {
        DM_640x480,
        640, 480,
        0,
        CT_VGA,
        0,
        524, 857,
        172, 40,
        21, 260,
        126, 837,
        36, 516,
        0, 1, 0
    },

    /* 640x480 NTSC 60Hz IL */
    /* DM_640x480_NTSC_IL */
    {
        DM_640x480,
        640, 480,
        VID_INTERLACE,
        CT_ANY,
        0,
        524, 857,
        164, 18,
        21, 260,
        126, 837,
        36, 516,
        0, 1, 0
    },

    /* 640x480 PAL 50Hz IL */
    /* DM_640x480_PAL_IL */
    {
        DM_640x480,
        640, 480,
        VID_INTERLACE | VID_PAL,
        CT_ANY,
        0,
        624, 863,
        174, 45,
        21, 260,
        141, 843,
        44, 620,
        0, 1, 0
    },

    /* 256x256 PAL 50Hz IL (seems to output the same w/o VID_PAL, ie. in NTSC IL mode) */
    /* DM_256x256_PAL_IL */
    {
        DM_256x256,
        256, 256,
        VID_PIXELDOUBLE | VID_LINEDOUBLE | VID_INTERLACE | VID_PAL,
        CT_ANY,
        0,
        624, 863,
        226, 37,
        21, 260,
        141, 843,
        44, 620,
        0, 1, 0
    },

    /* 768x480 NTSC 60Hz IL (thanks DCGrendel) */
    /* DM_768x480_NTSC_IL */
    {
        DM_768x480,
        768, 480,
        VID_INTERLACE,
        CT_ANY,
        0,
        524, 857,
        96, 18,
        21, 260,
        46, 837,
        36, 516,
        0, 1, 0
    },

    /* 768x576 PAL 50Hz IL (DCG) */
    /* DM_768x576_PAL_IL */
    {
        DM_768x576,
        768, 576,
        VID_INTERLACE | VID_PAL,
        CT_ANY,
        0,
        624, 863,
        88, 16,
        24, 260,
        54, 843,
        44, 620,
        0, 1, 0
    },

    /* 768x480 PAL 50Hz IL */
    /* DM_768x480_PAL_IL */
    {
        DM_768x480,
        768, 480,
        VID_INTERLACE | VID_PAL,
        CT_ANY,
        0,
        624, 863,
        88, 16,
        24, 260,
        54, 843,
        44, 620,
        0, 1, 0
    },

    /* 320x240 PAL 50Hz (thanks Marco Martins aka Mekanaizer) */
    /* DM_320x240_PAL */
    {
        DM_320x240,
        320, 240,
        VID_PIXELDOUBLE | VID_LINEDOUBLE | VID_PAL,
        CT_ANY,
        0,
        312, 863,
        174, 45,
        21, 260,
        141, 843,
        44, 620,
        0, 1, 0
    },

    /* END */
    /* DM_SENTINEL */
    { 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }

    /* DM_MODE_COUNT */
};

/*-----------------------------------------------------------------------------*/
static vid_mode_t  currmode = { 0 };
vid_mode_t  *vid_mode = 0;
uint16_t      *vram_s;
uint32_t      *vram_l;

/*-----------------------------------------------------------------------------*/
/* Checks the attached cable type (to the A/V port). Returns
   one of the following:
     0 == VGA
     1 == (nothing)
     2 == RGB
     3 == Composite

   This is a direct port of Marcus' assembly function of the
   same name.

   [This is the old KOS function by Megan.]
*/
int8_t vid_check_cable(void) {
    volatile uint32_t *porta = (volatile uint32_t *)0xff80002c;

    if(hardware_sys_mode(NULL) != HW_TYPE_RETAIL) {
        /* XXXX: This still needs to be figured out for NAOMI. For now, assume
           VGA mode. */
        return CT_VGA;
    }

    *porta = (*porta & 0xfff0ffff) | 0x000a0000;

    /* Read port8 and port9 */
    return (*((volatile uint16_t *)(porta + 1)) >> 8) & 3;
}

/*-----------------------------------------------------------------------------*/
int vid_set_mode_checked(int dm, vid_pixel_mode_t pm) {
    return vid_set_mode_standard_checked(dm, pm,
                                         VID_MODE_STANDARD_DEFAULT);
}

int vid_set_mode_standard_checked(int dm, vid_pixel_mode_t pm,
                                  vid_mode_standard_t standard) {
    vid_mode_t mode;
    int8_t ct = vid_check_cable();

    if(vid_mode_resolve(dm, pm, ct, standard, &mode) < 0) {
        dbglog(DBG_ERROR, "vid_set_mode: invalid mode %04x: %s\n", dm,
               strerror(errno));
        return -1;
    }

    return vid_set_mode_ex_checked(&mode);
}

void vid_set_mode(int dm, vid_pixel_mode_t pm) {
    (void)vid_set_mode_checked(dm, pm);
}

enum pvr_pm_modes {
    PVR_PM_XRGB1555,
    PVR_PM_RGB565,
    PVR_PM_ARGB4444,
    PVR_PM_ARGB1555,
    PVR_PM_RGB888,
    PVR_PM_XRGB8888,
    PVR_PM_ARGB8888,
};

static const unsigned int vid_bpp_to_pvr_cfg2[] = {
    [PM_RGB555] = PVR_PM_XRGB1555 | PVR_FB_CFG_2_DITHER,
    [PM_RGB565] = PVR_PM_RGB565 | PVR_FB_CFG_2_DITHER,
    [PM_RGB888P] = PVR_PM_RGB888,
    [PM_RGB0888] = PVR_PM_XRGB8888,
};

/*-----------------------------------------------------------------------------*/
static int vid_apply_mode(vid_mode_t *mode, int8_t ct) {
    uint32_t data;

    /* Suspend opt-in raster callbacks while scanout timing is rewritten. */
    vid_raster_mode_change_begin();

    /* Blank screen and reset display enable (looks nicer) */
    vid_set_enabled(false);

    /* Also clear any set border color now */
    vid_border_color(0, 0, 0);

    /* Clear interlace flag if VGA (this maybe should be in here?) */
    if(ct == CT_VGA) {
        mode->flags &= ~VID_INTERLACE;

        if(mode->flags & VID_LINEDOUBLE)
            mode->scanlines *= 2;
    }

    dbglog(DBG_INFO, "vid_set_mode: %ix%i%s %s with %i framebuffers.\n", mode->width, mode->height,
           (mode->flags & VID_INTERLACE) ? "IL" : "",
           (mode->cable_type == CT_VGA) ? "VGA" : (mode->flags & VID_PAL) ? "PAL" : "NTSC",
           mode->fb_count);

    /* Pixelformat */
    data = (mode->pm << 2);

    if(ct == CT_VGA) {
        data |= 1 << 23;

        if(mode->flags & VID_LINEDOUBLE)
            data |= 2;
    }

    PVR_SET(PVR_FB_CFG_1, data);
    PVR_SET(PVR_FB_CFG_2, vid_bpp_to_pvr_cfg2[mode->pm]);

    /* Linestride */
    PVR_SET(PVR_RENDER_MODULO, (mode->width * vid_pmode_bpp[mode->pm]) / 8);

    /* Display size */
    data = ((mode->width * vid_pmode_bpp[mode->pm]) / 4) - 1;

    if(ct == CT_VGA || (!(mode->flags & VID_INTERLACE))) {
        data |= (1 << 20) | ((mode->height - 1) << 10);
    }
    else {
        data |= (((mode->width * vid_pmode_bpp[mode->pm] >> 2) + 1) << 20)
                | (((mode->height / 2) - 1) << 10);
    }

    PVR_SET(PVR_FB_SIZE, data);

    /* vblank irq */
    if(ct == CT_VGA) {
        PVR_SET(PVR_VPOS_IRQ, (mode->scanint1 << 16) | (mode->scanint2 << 1));
    }
    else {
        PVR_SET(PVR_VPOS_IRQ, (mode->scanint1 << 16) | mode->scanint2);
    }

    /* Interlace stuff */
    data = 0x100;

    if(mode->flags & VID_INTERLACE) {
        data |= 0x10;

        if(mode->flags & VID_PAL) {
            data |= 0x80;
        }
        else {
            data |= 0x40;
        }
    }

    PVR_SET(PVR_IL_CFG, data);

    /* Border window */
    PVR_SET(PVR_BORDER_X, (mode->borderx1 << 16) | mode->borderx2);
    PVR_SET(PVR_BORDER_Y, (mode->bordery1 << 16) | mode->bordery2);

    /* Scanlines and clocks. */
    PVR_SET(PVR_SCAN_CLK, (mode->scanlines << 16) | mode->clocks);

    /* Horizontal pixel doubling */
    if(mode->flags & VID_PIXELDOUBLE) {
        PVR_SET(PVR_VIDEO_CFG, PVR_GET(PVR_VIDEO_CFG) | 0x100);
    }
    else {
        PVR_SET(PVR_VIDEO_CFG, PVR_GET(PVR_VIDEO_CFG) & ~0x100);
    }

    /* Bitmap window */
    PVR_SET(PVR_BITMAP_X, mode->bitmapx);

    /* The upper 16 bits map to field-2 and need to be one more for PAL */
    if(mode->flags & VID_PAL) {
        PVR_SET(PVR_BITMAP_Y, ((mode->bitmapy + 1) << 16) | mode->bitmapy);
    }
    else {
        PVR_SET(PVR_BITMAP_Y, (mode->bitmapy << 16) | mode->bitmapy);
    }

    /* Everything is ok */
    memcpy(&currmode, mode, sizeof(vid_mode_t));
    vid_mode = &currmode;

    /* Set up the framebuffer */
    vid_mode->fb_curr = ~0;
    vid_flip(0);

    /* Set cable type */
    *((volatile uint32_t *)0xa0702c00) = (*((volatile uint32_t *)0xa0702c00) & 0xfffffcff) |
        ((ct & 3) << 8);

    /* Re-enable the display */
    vid_set_enabled(true);
    vid_raster_mode_changed(vid_mode->scanlines);

    return 0;
}

int vid_set_mode_ex_checked(const vid_mode_t *mode) {
    vid_mode_t prepared;
    size_t frame_bytes;
    int8_t cable_type;

    if(!mode) {
        errno = EFAULT;
        return -1;
    }

    cable_type = vid_check_cable();
    if(vid_mode_validate_for_vram(mode, cable_type, PVR_RAM_SIZE,
                                  &frame_bytes) < 0) {
        dbglog(DBG_ERROR, "vid_set_mode_ex: invalid mode: %s\n",
               strerror(errno));
        return -1;
    }

    memcpy(&prepared, mode, sizeof(prepared));
    prepared.cable_type = cable_type;
    prepared.fb_size = frame_bytes;
    return vid_apply_mode(&prepared, cable_type);
}

void vid_set_mode_ex(vid_mode_t *mode) {
    (void)vid_set_mode_ex_checked(mode);
}

int vid_get_mode(vid_mode_t *mode) {
    if(!mode) {
        errno = EFAULT;
        return -1;
    }

    if(!vid_mode) {
        errno = ENODEV;
        return -1;
    }

    memcpy(mode, vid_mode, sizeof(*mode));
    return 0;
}

int vid_get_scanout_status(vid_scanout_status_t *status) {
    uint32_t raw;

    if(!status) {
        errno = EFAULT;
        return -1;
    }

    /* One read makes the five related fields a coherent observation. */
    raw = PVR_GET(PVR_SYNC_STATUS);
    status->scanline = FIELD_GET(raw, PVR_SYNC_STATUS_SCANLINE);
    status->field = (raw & PVR_SYNC_STATUS_FIELD) != 0;
    status->blank = (raw & PVR_SYNC_STATUS_BLANK) != 0;
    status->hsync = (raw & PVR_SYNC_STATUS_HSYNC) != 0;
    status->vsync = (raw & PVR_SYNC_STATUS_VSYNC) != 0;
    return 0;
}

int vid_get_display_filter(vid_display_filter_t *filter) {
    uint32_t fb_cfg;
    uint32_t scaler_cfg;

    if(!filter) {
        errno = EFAULT;
        return -1;
    }

    irq_disable_scoped();
    fb_cfg = PVR_GET(PVR_FB_CFG_2);
    scaler_cfg = PVR_GET(PVR_SCALER_CFG);

    filter->dithering = (fb_cfg & PVR_FB_CFG_2_DITHER) != 0;
    filter->antialiasing = (scaler_cfg & PVR_SCALER_CFG_FSAA) != 0;
    filter->vertical_scale =
        FIELD_GET(scaler_cfg, PVR_SCALER_CFG_VSCALE_FACTOR);
    return 0;
}

int vid_set_display_filter(const vid_display_filter_t *filter) {
    uint32_t fb_cfg;
    uint32_t scaler_cfg;
    bool current_antialiasing;

    if(!filter) {
        errno = EFAULT;
        return -1;
    }

    if(filter->vertical_scale == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Both controls are shared registers. Preserve fields owned by other
       framebuffer and scaler facilities while applying one coherent update. */
    irq_disable_scoped();
    fb_cfg = PVR_GET(PVR_FB_CFG_2);
    scaler_cfg = PVR_GET(PVR_SCALER_CFG);

    /* Full-scene antialiasing changes the TA's horizontal tile geometry and
       is therefore owned by pvr_init(). Updating only the scaler bit would
       leave the live render buffers and register layout inconsistent. Keep
       the field in this snapshot so callers can preserve/query it, but reject
       attempts to change it through the display-only setter. */
    current_antialiasing =
        (scaler_cfg & PVR_SCALER_CFG_FSAA) != 0;

    if(filter->antialiasing != current_antialiasing) {
        errno = ENOTSUP;
        return -1;
    }

    if(filter->dithering)
        fb_cfg |= PVR_FB_CFG_2_DITHER;
    else
        fb_cfg &= ~PVR_FB_CFG_2_DITHER;

    scaler_cfg &= ~PVR_SCALER_CFG_VSCALE_FACTOR;
    scaler_cfg |= FIELD_PREP(PVR_SCALER_CFG_VSCALE_FACTOR,
                             filter->vertical_scale);

    PVR_SET(PVR_FB_CFG_2, fb_cfg);
    PVR_SET(PVR_SCALER_CFG, scaler_cfg);
    return 0;
}

int vid_get_framebuffer_info(int32_t selector, vid_framebuffer_info_t *info) {
    uint32_t displayed_offset;
    uint32_t draw_offset;
    uint32_t selected_offset;
    uint32_t stride_bytes;
    uint32_t odd_field_offset = UINT32_MAX;
    size_t visible_bytes;
    size_t capacity_bytes = 0;
    uintptr_t draw_address;
    int32_t resolved_index = -1;
    bool draw_valid;
    irq_mask_t old_irq;

    if(info)
        memset(info, 0, sizeof(*info));

    if(!info) {
        errno = EFAULT;
        return -1;
    }

    if(selector < VID_FRAMEBUFFER_DRAW) {
        errno = EINVAL;
        return -1;
    }

    old_irq = irq_disable();
    if(!vid_mode) {
        irq_restore(old_irq);
        errno = ENODEV;
        return -1;
    }

    if(selector >= 0 && selector >= vid_mode->fb_count) {
        irq_restore(old_irq);
        errno = ERANGE;
        return -1;
    }

    stride_bytes = (uint32_t)vid_mode->width * vid_pmode_bpp[vid_mode->pm];
    visible_bytes = (size_t)stride_bytes * vid_mode->height;
    displayed_offset = PVR_GET(PVR_FB_ADDR) & (PVR_RAM_SIZE - 1u);
    draw_address = (uintptr_t)vram_l;
    draw_valid = draw_address >= PVR_RAM_BASE && draw_address < PVR_RAM_TOP;
    draw_offset = draw_valid ?
        (uint32_t)(draw_address - PVR_RAM_BASE) : UINT32_MAX;

    if(selector == VID_FRAMEBUFFER_DRAW && !draw_valid) {
        irq_restore(old_irq);
        errno = EIO;
        return -1;
    }

    if(selector == VID_FRAMEBUFFER_DISPLAYED)
        selected_offset = displayed_offset;
    else if(selector == VID_FRAMEBUFFER_DRAW)
        selected_offset = draw_offset;
    else
        selected_offset = (uint32_t)((size_t)selector * vid_mode->fb_size);

    /* Resolve active PVR-managed or caller-selected addresses to a configured
       slot only when they start at that slot's exact base. */
    if(vid_mode->fb_size &&
       selected_offset % vid_mode->fb_size == 0) {
        size_t candidate = selected_offset / vid_mode->fb_size;

        if(candidate < vid_mode->fb_count) {
            resolved_index = (int32_t)candidate;
            capacity_bytes = vid_mode->fb_size;
        }
    }

    if(selected_offset > PVR_RAM_SIZE ||
       visible_bytes > PVR_RAM_SIZE - selected_offset) {
        irq_restore(old_irq);
        errno = EIO;
        return -1;
    }

    if(vid_mode->flags & VID_INTERLACE) {
        if(selected_offset == displayed_offset)
            odd_field_offset = PVR_GET(PVR_FB_IL_ADDR) &
                               (PVR_RAM_SIZE - 1u);
        else
            odd_field_offset = selected_offset + stride_bytes;

        if(odd_field_offset >= PVR_RAM_SIZE) {
            irq_restore(old_irq);
            errno = EIO;
            return -1;
        }
    }

    *info = (vid_framebuffer_info_t) {
        .index = resolved_index,
        .vram_offset = selected_offset,
        .address = (void *)(PVR_RAM_BASE | selected_offset),
        .odd_field_offset = odd_field_offset,
        .visible_bytes = visible_bytes,
        .capacity_bytes = capacity_bytes,
        .stride_bytes = stride_bytes,
        .width = vid_mode->width,
        .height = vid_mode->height,
        .pixel_mode = vid_mode->pm,
        .displayed = selected_offset == displayed_offset,
        .draw_target = draw_valid && selected_offset == draw_offset,
        .interlaced = (vid_mode->flags & VID_INTERLACE) != 0
    };

    irq_restore(old_irq);
    return 0;
}

/*-----------------------------------------------------------------------------*/
void vid_set_vram(uint32_t base) {
    vram_s = (uint16_t*)(PVR_RAM_BASE | base);
    vram_l = (uint32_t*)(PVR_RAM_BASE | base);
}

void vid_set_start(uint32_t base) {
    /* Set vram base of current framebuffer */
    base &= (PVR_RAM_SIZE - 1);
    PVR_SET(PVR_FB_ADDR, base);

    vid_set_vram(base);

    /* Set odd-field if interlaced. */
    if(vid_mode->flags & VID_INTERLACE) {
        PVR_SET(PVR_FB_IL_ADDR, base + (vid_mode->width * vid_pmode_bpp[vid_mode->pm]));
    }
}

uint32_t vid_get_start(int32_t fb) {
    /* If out of bounds, return current fb addr */
    if((fb < 0) || (fb >= vid_mode->fb_count)) {
        fb = vid_mode->fb_curr;
    }

    return (vid_mode->fb_size * fb);
}

/*-----------------------------------------------------------------------------*/
void vid_set_fb(int32_t fb) {
    uint16_t oldfb = vid_mode->fb_curr;

    if((fb < 0) || (fb >= vid_mode->fb_count)) {
        vid_mode->fb_curr++;
    }
    else {
        vid_mode->fb_curr = fb;
    }

    /* Roll over */
    vid_mode->fb_curr = vid_mode->fb_curr % vid_mode->fb_count;

    if(vid_mode->fb_curr == oldfb) {
        return;
    }

    vid_set_start(vid_get_start(vid_mode->fb_curr));
}

/*-----------------------------------------------------------------------------*/
void vid_flip(int32_t fb) {
    uint32_t base;

    vid_set_fb(fb);

    /* Set the vram_* pointers to the next fb */
    base = vid_get_start(((vid_mode->fb_curr + 1) % vid_mode->fb_count));
    vid_set_vram(base);
}

/*-----------------------------------------------------------------------------*/
uint32_t vid_border_color(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t obc = PVR_GET(PVR_BORDER_COLOR);
    PVR_SET(PVR_BORDER_COLOR, ((r & 0xFF) << 16) |
                       ((g & 0xFF) << 8) |
                       (b & 0xFF));
    return obc;
}

/*-----------------------------------------------------------------------------*/
/* Clears the screen with a given color */
void vid_clear(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t pixel16;
    uint32_t pixel32;

    switch(vid_mode->pm) {
        case PM_RGB555:
            pixel16 = ((r >> 3) << 10)
                      | ((g >> 3) << 5)
                      | ((b >> 3) << 0);
            sq_set16(vram_s, pixel16, (vid_mode->width * vid_mode->height) * vid_pmode_bpp[PM_RGB555]);
            break;
        case PM_RGB565:
            pixel16 = ((r >> 3) << 11)
                      | ((g >> 2) << 5)
                      | ((b >> 3) << 0);
            sq_set16(vram_s, pixel16, (vid_mode->width * vid_mode->height) * vid_pmode_bpp[PM_RGB565]);
            break;
        case PM_RGB888P:
            /* Need to come up with some way to fill this quickly. */
            dbglog(DBG_WARNING, "vid_clear: PM_RGB888P not supported, clearing with 0\n");
            sq_set32(vram_l, 0, (vid_mode->width * vid_mode->height) * vid_pmode_bpp[PM_RGB888P]);
            break;
        case PM_RGB0888:
            pixel32 = (r << 16) | (g << 8) | (b << 0);
            sq_set32(vram_l, pixel32, (vid_mode->width * vid_mode->height) * vid_pmode_bpp[PM_RGB0888]);
            break;
        default:
            dbglog(DBG_ERROR, "vid_clear: Invalid Pixel Mode: %i\n", vid_mode->pm);
            break;
    }
}

/*-----------------------------------------------------------------------------*/
/* Clears all of video memory as quickly as possible */
void vid_empty(void) {
    sq_clr((uint32_t *)PVR_RAM_BASE, PVR_RAM_SIZE);
}

/*-----------------------------------------------------------------------------*/
bool vid_get_enabled(void) {
    if(PVR_GET(PVR_FB_CFG_1) & 1) return true;
    else return false;
}

void vid_set_enabled(bool val) {
    /* If it's already the current setting, dont' do anything */
    if(val == vid_get_enabled()) return;

    if(val) {
        /* Re-enable the display */
        PVR_SET(PVR_VIDEO_CFG, PVR_GET(PVR_VIDEO_CFG) & ~8);
        PVR_SET(PVR_FB_CFG_1, PVR_GET(PVR_FB_CFG_1) | 1);
    }
    else {
        /* Blank screen and reset display enable (looks nicer) */
        PVR_SET(PVR_VIDEO_CFG, PVR_GET(PVR_VIDEO_CFG) | 8);    /* Blank */
        PVR_SET(PVR_FB_CFG_1, PVR_GET(PVR_FB_CFG_1) & ~1);     /* Display disable */
    }
}

/*-----------------------------------------------------------------------------*/
/* Waits for a vertical refresh to start. This is the period between
   when the scan beam reaches the bottom of the picture, and when it
   starts again at the top.

   Thanks to HeroZero for this info.

*/
void vid_waitvbl(void) {
    while(!(PVR_GET(PVR_SYNC_STATUS) & 0x01ff))
        ;

    while(PVR_GET(PVR_SYNC_STATUS) & 0x01ff)
        ;
}

/*-----------------------------------------------------------------------------*/
int vid_init_checked(int disp_mode, vid_pixel_mode_t pixel_mode) {
    if(vid_set_mode_checked(disp_mode, pixel_mode) < 0)
        return -1;

    vid_empty();
    return 0;
}

void vid_init(int disp_mode, vid_pixel_mode_t pixel_mode) {
    (void)vid_init_checked(disp_mode, pixel_mode);
}

/*-----------------------------------------------------------------------------*/
void vid_shutdown(void) {
    vid_raster_shutdown();

    /* Reset back to default mode, in case we're going back to a loader. */
    vid_init(DM_640x480, PM_RGB565);
}

void vid_set_dithering(bool enable) {
    uint32_t cfg;

    irq_disable_scoped();
    cfg = PVR_GET(PVR_FB_CFG_2);

    if(enable)
        cfg |= PVR_FB_CFG_2_DITHER;
    else
        cfg &= ~PVR_FB_CFG_2_DITHER;

    PVR_SET(PVR_FB_CFG_2, cfg);
}
