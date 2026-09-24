/* KallistiOS ##version##

   mode-policy.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/flashrom.h>

#include <stdio.h>

static const char *cable_name(int8_t cable) {
    switch(cable) {
        case CT_VGA:
            return "VGA";
        case CT_NONE:
            return "none";
        case CT_RGB:
            return "RGB";
        case CT_COMPOSITE:
            return "composite/RF";
        default:
            return "unknown";
    }
}

static void print_mode(const char *label, const vid_mode_t *mode) {
    printf("%s: %ux%u %s %u Bpp, %u framebuffer(s)\n", label,
           mode->width, mode->height,
           mode->cable_type == CT_VGA ? "VGA" :
           (mode->flags & VID_PAL) ? "50 Hz" : "60 Hz",
           vid_pmode_bpp[mode->pm], mode->fb_count);
}

int main(void) {
    vid_mode_t current, candidate;
    vid_mode_standard_t standard = VID_MODE_STANDARD_60HZ;
    int8_t cable = vid_check_cable();
    int region = flashrom_get_region();

    printf("KallistiOS ##version##\n\n");
    printf("Cable: %s (%d)\n", cable_name(cable), cable);

    if(vid_get_mode(&current) == 0)
        print_mode("Current", &current);
    else
        perror("vid_get_mode");

    if(region == FLASHROM_REGION_EUROPE)
        standard = VID_MODE_STANDARD_50HZ;
    else if(region < 0 || region == FLASHROM_REGION_UNKNOWN)
        printf("Region unavailable; retaining explicit 60 Hz policy\n");

    printf("Proposed policy: %s\n",
           standard == VID_MODE_STANDARD_50HZ ? "50 Hz" : "60 Hz");
    if(vid_mode_resolve(DM_640x480, PM_RGB565, cable, standard,
                        &candidate) < 0) {
        perror("vid_mode_resolve");
        return 1;
    }

    print_mode("Candidate (not installed)", &candidate);
    return 0;
}
