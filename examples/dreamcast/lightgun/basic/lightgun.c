/* KallistiOS ##version##

   lightgun.c
   Copyright (C) 2015 Lawrence Sebald
*/

#include <stdio.h>
#include <kos/dbgio.h>

#include <dc/biosfont.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/lightgun.h>
#include <dc/pvr.h>
#include <dc/video.h>

/* This little example isn't exactly anything fancy, but it does demonstrate the
   basics of getting the light gun up and running.

   Some things to note about using the light gun:
   1. Things work better if the player is aiming at a very bright section of the
      screen. The light-gun driver flashes each capture field automatically,
      and this example also keeps the play area white.
   2. The light gun will (of course) only work with CRT televisions or monitors.
   3. You can only poll one gun at a time. This is a hardware limitation. You
      can switch back and forth between guns, but only one can actively be
      reading its position at a time.
   4. The light gun itself only returns data as if it were a normal controller.
      The trigger is mapped to the A button and you also have the D-Pad, B, and
      Start buttons, generally.
*/

int main(int argc, char *argv[]) {
    int x, y;
    uint32_t last_capture = 0;
    maple_device_t *dev;
    cont_snapshot_t controller;
    lightgun_snapshot_t gun;

    (void)argc;
    (void)argv;

    /* Do any printing to the screen and make it be black text on a white
       background (as much as we can anyway). I should eventually make it so you
       can specify this in fb_console... */
    dbgio_dev_select("fb");
    bfont_set_foreground_color(0x00000000);
    bfont_set_background_color(0xFFFFFFFF);
    pvr_init_defaults();

    /* Trigger edges on any connected gun now schedule exclusive
       light-gun captures. Port A wins if multiple guns fire together. */
    lightgun_set_enabled_ports(LIGHTGUN_PORT_ALL);

    /* Blank the whole screen to white. */
    for(y = 0; y < 480; ++y) {
        for(x = 0; x < 640; ++x) {
            vram_s[y * 640 + x] = 0xFFFF;
        }
    }

    for(;;) {
        /* Wait for vblank... */
        vid_waitvbl();
        /* Blank the "play" area of the screen to white. */
        for(y = 0; y < 480; ++y) {
            for(x = 128; x < 640; ++x) {
                vram_s[y * 640 + x] = 0xFFFF;
            }
        }

        /* A snapshot is published coherently when the special Maple capture
           completes. Values are raw PVR counters and need game calibration. */
        if(lightgun_get_snapshot(&gun) == 0 &&
           gun.sequence != last_capture) {
            printf("port %c: %d %d\n", 'A' + gun.port, gun.x, gun.y);
            last_capture = gun.sequence;
        }

        /* Grab the light gun and poll it for whether any interesting buttons
           are pressed. */
        if((dev = maple_enum_type(0, MAPLE_FUNC_LIGHTGUN))) {
            /* Button state remains the normal controller function. The driver
               already used the coherent A-button edge to schedule the aim
               capture; the application only needs Start here. */
            if(cont_get_snapshot(dev, &controller) == 0 &&
               (controller.state.buttons & CONT_START))
                break;
        }
    }

    lightgun_set_enabled_ports(0);
    return 0;
}
