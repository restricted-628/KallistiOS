/* KallistiOS ##version##

   main.c
   Copyright (C) 2023 Andy Barajas
   Copyright (C) 2026 Joseph Black

   This example program simply demonstrations how to load and play
   sound effects on their own channels as well as on the same channel.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <kos/init.h>
#include <dc/biosfont.h>
#include <dc/video.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#define LEFT 0
#define CENTER 128
#define RIGHT 255

static void draw_instructions(uint8_t volume);
static sfxhnd_t load_sfx(const char *path);

static cont_state_t *get_cont_state(void);
static int button_pressed(uint32_t current_buttons, uint32_t changed_buttons, uint32_t button);

int main(int argc, char **argv) {
    uint8_t volume = 128;
    int volume_changed = 1;
    cont_state_t *cond;

    vid_set_mode(DM_640x480, PM_RGB555);
    /* Initialize the sound system. */
    if(snd_init() < 0) {
        perror("snd_init");
        return EXIT_FAILURE;
    }

    /* Load WAV files from memory with their actual accessible sizes. */
    sfxhnd_t beep1 = load_sfx("/rd/beep-1.wav");
    sfxhnd_t beep2 = load_sfx("/rd/beep-2.wav");
    sfxhnd_t beep3 = load_sfx("/rd/beep-3.wav");
    sfxhnd_t beep4 = load_sfx("/rd/beep-4.wav");

    if(!beep1 || !beep2 || !beep3 || !beep4) {
        fprintf(stderr, "Unable to load all sound effects: %s\n",
                strerror(errno));
        snd_sfx_unload_all();
        snd_shutdown();
        return EXIT_FAILURE;
    }

    uint32_t current_buttons = 0;
    uint32_t changed_buttons = 0;
    uint32_t previous_buttons = 0;

    for(;;) {
        if(!(cond = get_cont_state()))
            continue;
        current_buttons = cond->buttons;
        changed_buttons = current_buttons ^ previous_buttons;
        previous_buttons = current_buttons;

        // Play sounds on different channels
        if(button_pressed(current_buttons, changed_buttons, CONT_A)) {
            snd_sfx_play(beep1, volume, CENTER);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_B)) {
            snd_sfx_play(beep2, volume, RIGHT);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_X)) {
            snd_sfx_play(beep3, volume, LEFT);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_Y)) {
            snd_sfx_play(beep4, volume, CENTER);
        }

        // Play sounds on same channel
        if(button_pressed(current_buttons, changed_buttons, CONT_DPAD_DOWN)) {
            snd_sfx_play_chn(0, beep1, volume, CENTER);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_DPAD_RIGHT)) {
            snd_sfx_play_chn(0, beep2, volume, RIGHT);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_DPAD_LEFT)) {
            snd_sfx_play_chn(0, beep3, volume, LEFT);
        }
        if(button_pressed(current_buttons, changed_buttons, CONT_DPAD_UP)) {
            snd_sfx_play_chn(0, beep4, volume, CENTER);
        }

        // Adjust Volume
        if(cond->ltrig > 0) {
            volume_changed = 1;

            if(volume < 255)
                volume++;
        }
        if(cond->rtrig > 0) {
            volume_changed = 1;

            if(volume > 0)
                volume--;
        }

        // Exit Program
        if(button_pressed(current_buttons, changed_buttons, CONT_START))
            break;

        if(volume_changed) {
            volume_changed = 0;
            draw_instructions(volume);
        }
    }

    /* Unload all sound effects from sound RAM. */
    snd_sfx_unload_all();

    snd_shutdown();

    return 0;
}

static sfxhnd_t load_sfx(const char *path) {
    void *buffer = NULL;
    ssize_t size;
    sfxhnd_t effect;

    size = fs_load(path, &buffer);

    if(size <= 0)
        return SFXHND_INVALID;

    effect = snd_sfx_load_wav_buf(buffer, (size_t)size);
    free(buffer);
    return effect;
}

static void draw_instructions(uint8_t volume) {
    int x = 20, y = 20+24;
    int color = 1;
    char current_volume_str[32];

    memset(current_volume_str, 0, 32);
    snprintf(current_volume_str, 32, "Current Volume: %3i", volume);

    bfont_draw_str(vram_s + y*640+x, 640, color, "Press A,B,X,Y to play beeps on separate channels");
    y += 48;
    bfont_draw_str(vram_s + y*640+x, 640, color, "Press UP,DOWN,LEFT,RIGHT on D-Pad to play beeps");
    y += 24;
    bfont_draw_str(vram_s + y*640+x, 640, color, "on the same channel");
    y += 48;
    bfont_draw_str(vram_s + y*640+x, 640, color, "Press L-Trigger/R-Trigger to +/- volume");
    y += 24;
    bfont_draw_str(vram_s + y*640+x, 640, color, current_volume_str);
    y += 48;
    bfont_draw_str(vram_s + y*640+x, 640, color, "Press Start to exit program");
}

static cont_state_t *get_cont_state(void) {
    maple_device_t *cont;
    cont_state_t *state;

    cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if(cont) {
        state = (cont_state_t*)maple_dev_status(cont);
        return state;
    }

    return NULL;
}

static int button_pressed(uint32_t current_buttons, uint32_t changed_buttons, uint32_t button) {
    if(changed_buttons & button) {
        if (current_buttons & button)
            return 1;
    }

    return 0;
}
