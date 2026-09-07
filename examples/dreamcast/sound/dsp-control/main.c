/* KallistiOS ##version##

   main.c
   Copyright (C) 2026 Joseph Black

   Exercise checked DSP state and effect-return control without effect
   content or audible output.
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kos.h>

static void show_result(int passed) {
    vid_clear(passed ? 0 : 96, passed ? 96 : 0, 0);
    bfont_draw_str(vram_s + vid_mode->width * 80 + 24,
                   vid_mode->width, true,
                   passed ? "DSP CONTROL TEST: PASS"
                          : "DSP CONTROL TEST: FAIL");
    bfont_draw_str(vram_s + vid_mode->width * 112 + 24,
                   vid_mode->width, true,
                   "Validation, state, output, and bounded clear");
    thd_sleep(passed ? 5000 : 10000);
}

int main(int argc, char **argv) {
    snd_dsp_program_t program;
    snd_dsp_output_t output = { .level = 0, .pan = 192 };
    snd_dsp_output_t observed;
    snd_dsp_status_t status;
    size_t required_work;
    int result = EXIT_FAILURE;

    (void)argc;
    (void)argv;

    vid_set_mode(DM_640x480, PM_RGB565);
    if(snd_init() < 0) {
        perror("snd_init");
        show_result(false);
        return EXIT_FAILURE;
    }

    if(snd_dsp_program_init(&program) < 0) {
        perror("snd_dsp_program_init");
        goto cleanup;
    }

    /* ZERO is a defined arithmetic control which makes the image present for
       structural validation. The synthetic image is never installed. */
    program.steps[0].word[2] = UINT16_C(0x0002);
    if(snd_dsp_program_validate(&program, &required_work) < 0) {
        perror("snd_dsp_program_validate");
        goto cleanup;
    }
    if(required_work != 16384u) {
        errno = EPROTO;
        perror("DSP work-area geometry");
        goto cleanup;
    }

    if(snd_dsp_get_status(&status) < 0) {
        perror("snd_dsp_get_status");
        goto cleanup;
    }
    if(status.loaded) {
        errno = EBUSY;
        perror("unexpected installed DSP program");
        goto cleanup;
    }

    if(snd_dsp_output_set(0, &output) < 0 ||
       snd_dsp_output_get(0, &observed) < 0) {
        perror("DSP effect-return control");
        goto cleanup;
    }
    if(observed.level != output.level || observed.pan != output.pan) {
        errno = EPROTO;
        perror("DSP effect-return copy-out");
        goto cleanup;
    }

    if(snd_dsp_program_clear(100) < 0) {
        perror("snd_dsp_program_clear");
        goto cleanup;
    }

    puts("PASS: checked DSP validation, status, output, and clear");
    result = EXIT_SUCCESS;

cleanup:
    show_result(result == EXIT_SUCCESS);
    snd_shutdown();
    return result;
}
