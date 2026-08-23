/* KallistiOS ##version##

   framebuffer-query.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int expect_errno(int result, int expected, const char *operation) {
    if(result == -1 && errno == expected)
        return 0;

    printf("FAIL: %s returned %d with errno %d, expected %d\n",
           operation, result, errno, expected);
    return -1;
}

static int validate_geometry(const vid_framebuffer_info_t *info,
                             const vid_mode_t *mode) {
    size_t expected_visible =
        (size_t)mode->width * vid_pmode_bpp[mode->pm] * mode->height;

    return info->width == mode->width && info->height == mode->height &&
           info->pixel_mode == mode->pm &&
           info->stride_bytes == mode->width * vid_pmode_bpp[mode->pm] &&
           info->visible_bytes == expected_visible &&
           info->vram_offset <= PVR_RAM_SIZE &&
           info->visible_bytes <= PVR_RAM_SIZE - info->vram_offset &&
           info->address == (void *)(PVR_RAM_BASE | info->vram_offset) &&
           info->interlaced == !!(mode->flags & VID_INTERLACE);
}

int main(void) {
    vid_mode_t mode;
    vid_framebuffer_info_t displayed;
    vid_framebuffer_info_t drawing;
    vid_framebuffer_info_t slot;
    vid_framebuffer_info_t rejected;
    vid_framebuffer_info_t pvr_displayed;
    vid_framebuffer_info_t pvr_drawing;
    uint32_t original_draw_offset;

    printf("KallistiOS ##version##\n\n");

    if(vid_get_mode(&mode) < 0) {
        perror("vid_get_mode");
        return 1;
    }

    errno = 0;
    if(expect_errno(vid_get_framebuffer_info(
                        VID_FRAMEBUFFER_DISPLAYED, NULL), EFAULT,
                    "vid_get_framebuffer_info(NULL)") < 0)
        return 1;

    memset(&rejected, 0x55, sizeof(rejected));
    errno = 0;
    if(expect_errno(vid_get_framebuffer_info(-3, &rejected), EINVAL,
                    "invalid framebuffer selector") < 0 ||
       rejected.address != NULL || rejected.visible_bytes != 0) {
        puts("FAIL: rejected query did not leave a cleared result");
        return 1;
    }

    errno = 0;
    if(expect_errno(vid_get_framebuffer_info(mode.fb_count, &rejected),
                    ERANGE, "out-of-range framebuffer index") < 0)
        return 1;

    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED, &displayed) < 0 ||
       !validate_geometry(&displayed, &mode) || !displayed.displayed) {
        puts("FAIL: invalid displayed-surface description");
        return 1;
    }

    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DRAW, &drawing) < 0 ||
       !validate_geometry(&drawing, &mode) || !drawing.draw_target) {
        puts("FAIL: invalid drawing-surface description");
        return 1;
    }

    if(vid_get_framebuffer_info(0, &slot) < 0 ||
       !validate_geometry(&slot, &mode) || slot.index != 0 ||
       slot.vram_offset != 0 || slot.capacity_bytes != mode.fb_size) {
        puts("FAIL: invalid configured-slot description");
        return 1;
    }

    original_draw_offset = drawing.vram_offset;
    vid_set_vram(vid_get_start(0));
    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DRAW, &drawing) < 0 ||
       drawing.index != 0 || drawing.capacity_bytes != mode.fb_size ||
       !drawing.draw_target) {
        vid_set_vram(original_draw_offset);
        puts("FAIL: drawing target did not resolve to configured slot zero");
        return 1;
    }
    vid_set_vram(original_draw_offset);

    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DRAW, &drawing) < 0 ||
       drawing.vram_offset != original_draw_offset || !drawing.draw_target) {
        puts("FAIL: original drawing target was not restored");
        return 1;
    }

    if(pvr_init_defaults() < 0) {
        perror("pvr_init_defaults");
        return 1;
    }

    if(vid_get_framebuffer_info(VID_FRAMEBUFFER_DISPLAYED,
                                &pvr_displayed) < 0 ||
       vid_get_framebuffer_info(VID_FRAMEBUFFER_DRAW, &pvr_drawing) < 0 ||
       !validate_geometry(&pvr_displayed, &mode) ||
       !validate_geometry(&pvr_drawing, &mode) ||
       !pvr_displayed.displayed || !pvr_drawing.draw_target ||
       pvr_displayed.index != -1 || pvr_drawing.index != -1 ||
       pvr_displayed.capacity_bytes != 0 ||
       pvr_drawing.capacity_bytes != 0) {
        puts("FAIL: PVR-managed framebuffer acquired a fabricated slot");
        pvr_shutdown();
        return 1;
    }

    printf("Displayed: offset=%#" PRIx32
           " index=%ld visible=%zu capacity=%zu\n",
           displayed.vram_offset, (long)displayed.index,
           displayed.visible_bytes, displayed.capacity_bytes);
    printf("Drawing:   offset=%#" PRIx32
           " index=%ld visible=%zu capacity=%zu\n",
           drawing.vram_offset, (long)drawing.index,
           drawing.visible_bytes, drawing.capacity_bytes);
    printf("Slot 0:    offset=%#" PRIx32
           " index=%ld visible=%zu capacity=%zu\n",
           slot.vram_offset, (long)slot.index,
           slot.visible_bytes, slot.capacity_bytes);
    printf("PVR view:  offset=%#" PRIx32
           " index=%ld visible=%zu capacity=%zu\n",
           pvr_displayed.vram_offset, (long)pvr_displayed.index,
           pvr_displayed.visible_bytes, pvr_displayed.capacity_bytes);
    pvr_shutdown();
    puts("PASS: checked framebuffer-surface query validation complete");
    return 0;
}
