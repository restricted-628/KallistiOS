/* KallistiOS ##version##

   Host-side checked DSP program validation tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/sound/dsp.h>

#define DSP_TABLE UINT16_C(0x8000)
#define DSP_READ UINT16_C(0x2000)
#define DSP_EFFECT_WRITE UINT16_C(0x1000)
#define DSP_ZERO UINT16_C(0x0002)
#define DSP_MASA(index) ((uint16_t)((index) << 9))
#define DSP_NO_FLOAT UINT16_C(0x8000)
#define DSP_ADREB UINT16_C(0x0100)
#define DSP_NXADR UINT16_C(0x0080)

static snd_dsp_program_t valid_program(void) {
    snd_dsp_program_t program;

    assert(snd_dsp_program_init(&program) == 0);
    /* ZERO is a defined arithmetic control which distinguishes a present
       image from the all-zero stopped state without requesting memory I/O. */
    program.steps[0].word[2] = DSP_ZERO;
    return program;
}

static void expect_invalid(snd_dsp_program_t *program) {
    size_t required = SIZE_MAX;

    errno = 0;
    assert(snd_dsp_program_validate(program, &required) == -1);
    assert(errno == EINVAL);
    assert(required == 0);
}

static void test_initialization_and_arguments(void) {
    snd_dsp_program_t program;
    size_t required = SIZE_MAX;

    errno = 0;
    assert(snd_dsp_program_init(NULL) == -1);
    assert(errno == EINVAL);

    assert(snd_dsp_program_init(&program) == 0);
    assert(program.ring_size == SND_DSP_RING_16K);
    assert(program.work_initial_data == NULL);
    assert(program.work_initial_bytes == 0);
    expect_invalid(&program);

    errno = 0;
    assert(snd_dsp_program_validate(NULL, &required) == -1);
    assert(errno == EINVAL);
    assert(required == 0);
}

static void test_ring_and_initial_data(void) {
    static const uint8_t initial[20000] = {0};
    snd_dsp_program_t program = valid_program();
    size_t required;
    unsigned int ring;

    for(ring = SND_DSP_RING_16K; ring <= SND_DSP_RING_128K; ++ring) {
        program.ring_size = (snd_dsp_ring_size_t)ring;
        assert(snd_dsp_program_validate(&program, &required) == 0);
        assert(required == ((size_t)16384u << ring));
    }

    program.ring_size = SND_DSP_RING_16K;
    program.work_initial_data = initial;
    program.work_initial_bytes = sizeof(initial);
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == sizeof(initial));

    program.work_initial_data = NULL;
    expect_invalid(&program);
    program.work_initial_data = initial;
    program.work_initial_bytes = SND_DSP_MAX_WORK_BYTES + 1u;
    expect_invalid(&program);
    program.work_initial_bytes = 0;
    program.ring_size = (snd_dsp_ring_size_t)4;
    expect_invalid(&program);
}

static void test_coefficients_and_step_placement(void) {
    snd_dsp_program_t program = valid_program();

    program.coefficients[17] = 7;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[2] = DSP_READ;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[2] = DSP_TABLE;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[3] = DSP_MASA(1);
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[3] = DSP_NO_FLOAT;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[0] = 1;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[1] = 1;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[3] = 1;
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[1] = UINT16_C(0x1900);
    expect_invalid(&program);

    program = valid_program();
    program.steps[0].word[1] = UINT16_C(0x1880);
    assert(snd_dsp_program_validate(&program, NULL) == 0);

    /* Effect-register writes share the instruction word with memory request
       bits but are legal on either step parity. */
    program = valid_program();
    program.steps[0].word[2] = DSP_EFFECT_WRITE;
    assert(snd_dsp_program_validate(&program, NULL) == 0);
}

static void test_memory_bounds(void) {
    snd_dsp_program_t program = valid_program();
    size_t required;

    program.addresses[3] = UINT16_C(0x2345);
    program.steps[1].word[2] = DSP_TABLE | DSP_READ;
    program.steps[1].word[3] = DSP_MASA(3);
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == ((size_t)UINT16_C(0x2345) + 1u) * 2u);

    program.steps[1].word[3] = DSP_MASA(3) | DSP_NXADR;
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == ((size_t)UINT16_C(0x2346) + 1u) * 2u);

    program.steps[1].word[3] = DSP_MASA(3) | DSP_ADREB;
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == ((size_t)UINT16_C(0x3344) + 1u) * 2u);

    program.addresses[3] = UINT16_C(0xf100);
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == SND_DSP_MAX_WORK_BYTES);

    program.addresses[3] = UINT16_MAX;
    program.steps[1].word[3] = DSP_MASA(3) | DSP_NXADR;
    assert(snd_dsp_program_validate(&program, &required) == 0);
    assert(required == 16384u);
}

int main(void) {
    test_initialization_and_arguments();
    test_ring_and_initial_data();
    test_coefficients_and_step_placement();
    test_memory_bounds();
    puts("Checked DSP program validation tests passed");
    return 0;
}
