/* KallistiOS ##version##

   snd_dsp_program.c
   Copyright (C) 2026 Joseph Black

   Hardware-independent DSP image validation.
*/

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <dc/sound/dsp.h>

#define DSP_PROGRAM_TABLE UINT16_C(0x8000)
#define DSP_PROGRAM_WRITE UINT16_C(0x4000)
#define DSP_PROGRAM_READ  UINT16_C(0x2000)
#define DSP_PROGRAM_MEMORY_WORD_MASK \
    (DSP_PROGRAM_TABLE | DSP_PROGRAM_WRITE | DSP_PROGRAM_READ)
#define DSP_PROGRAM_RESERVED_WORD0 UINT16_C(0x0001)
#define DSP_PROGRAM_INPUT_ADDRESS_MASK UINT16_C(0x1f80)
#define DSP_PROGRAM_INPUT_ADDRESS_SHIFT 7
#define DSP_PROGRAM_RESERVED_WORD1 UINT16_C(0x0001)
#define DSP_PROGRAM_NO_FLOAT UINT16_C(0x8000)
#define DSP_PROGRAM_MASA_MASK UINT16_C(0x7e00)
#define DSP_PROGRAM_MASA_SHIFT 9
#define DSP_PROGRAM_ADDRESS_REGISTER UINT16_C(0x0100)
#define DSP_PROGRAM_NEXT_ADDRESS UINT16_C(0x0080)
#define DSP_PROGRAM_MEMORY_CONTROL_MASK \
    (DSP_PROGRAM_NO_FLOAT | DSP_PROGRAM_MASA_MASK | \
     DSP_PROGRAM_ADDRESS_REGISTER | DSP_PROGRAM_NEXT_ADDRESS)
#define DSP_PROGRAM_RESERVED_WORD3 UINT16_C(0x007f)
#define DSP_PROGRAM_LAST_INPUT UINT16_C(0x0031)
#define DSP_ADDRESS_REGISTER_MAX UINT32_C(0x0fff)
#define DSP_WORD_ADDRESS_MAX UINT32_C(0xffff)

static size_t ring_bytes(snd_dsp_ring_size_t size) {
    if(size < SND_DSP_RING_16K || size > SND_DSP_RING_128K)
        return 0;

    return (size_t)16384u << (unsigned int)size;
}

int snd_dsp_program_init(snd_dsp_program_t *program) {
    if(!program) {
        errno = EINVAL;
        return -1;
    }

    memset(program, 0, sizeof(*program));
    program->ring_size = SND_DSP_RING_16K;
    return 0;
}

int snd_dsp_program_validate(const snd_dsp_program_t *program,
                             size_t *required_work_bytes) {
    size_t required;
    size_t coefficient;
    size_t step;
    bool nonzero_program = false;

    if(required_work_bytes)
        *required_work_bytes = 0;

    if(!program || !(required = ring_bytes(program->ring_size)) ||
       (program->work_initial_bytes && !program->work_initial_data) ||
       program->work_initial_bytes > SND_DSP_MAX_WORK_BYTES) {
        errno = EINVAL;
        return -1;
    }

    if(program->work_initial_bytes > required)
        required = program->work_initial_bytes;

    for(coefficient = 0; coefficient < SND_DSP_COEFFICIENT_COUNT;
        ++coefficient) {
        /* Only bits 15:3 are coefficient data. Requiring the reserved low
           bits to be zero keeps an image forward-compatible with wider
           coefficient implementations. */
        if(program->coefficients[coefficient] & UINT16_C(0x0007)) {
            errno = EINVAL;
            return -1;
        }
    }

    for(step = 0; step < SND_DSP_STEP_COUNT; ++step) {
        const snd_dsp_instruction_t *instruction = &program->steps[step];
        uint16_t memory = instruction->word[2];
        uint16_t control = instruction->word[3];
        uint16_t input_address =
            (instruction->word[1] & DSP_PROGRAM_INPUT_ADDRESS_MASK) >>
            DSP_PROGRAM_INPUT_ADDRESS_SHIFT;
        bool accesses_memory =
            (memory & (DSP_PROGRAM_READ | DSP_PROGRAM_WRITE)) != 0;
        bool has_memory_control =
            (memory & DSP_PROGRAM_MEMORY_WORD_MASK) != 0 ||
            (control & DSP_PROGRAM_MEMORY_CONTROL_MASK) != 0;
        size_t word;

        for(word = 0; word < SND_DSP_STEP_WORDS; ++word)
            nonzero_program |= instruction->word[word] != 0;

        if((instruction->word[0] & DSP_PROGRAM_RESERVED_WORD0) ||
           (instruction->word[1] & DSP_PROGRAM_RESERVED_WORD1) ||
           (control & DSP_PROGRAM_RESERVED_WORD3) ||
           input_address > DSP_PROGRAM_LAST_INPUT) {
            errno = EINVAL;
            return -1;
        }

        /* Sound-memory requests are admitted only on odd zero-based steps.
           Rejecting illegal placement prevents a malformed image from
           depending on undefined bus timing. */
        if(has_memory_control && !(step & 1u)) {
            errno = EINVAL;
            return -1;
        }

        if(accesses_memory && (memory & DSP_PROGRAM_TABLE)) {
            uint32_t address_index =
                (control & DSP_PROGRAM_MASA_MASK) >>
                DSP_PROGRAM_MASA_SHIFT;
            uint32_t first = program->addresses[address_index];
            uint32_t last;
            size_t bytes;

            if(control & DSP_PROGRAM_NEXT_ADDRESS)
                first = (first + 1u) & DSP_WORD_ADDRESS_MAX;

            if(control & DSP_PROGRAM_ADDRESS_REGISTER) {
                if(first > DSP_WORD_ADDRESS_MAX - DSP_ADDRESS_REGISTER_MAX)
                    last = DSP_WORD_ADDRESS_MAX;
                else
                    last = first + DSP_ADDRESS_REGISTER_MAX;
            }
            else {
                last = first;
            }

            bytes = ((size_t)last + 1u) * sizeof(uint16_t);
            if(bytes > required)
                required = bytes;
        }
    }

    if(!nonzero_program || required > SND_DSP_MAX_WORK_BYTES) {
        errno = EINVAL;
        return -1;
    }

    if(required_work_bytes)
        *required_work_bytes = required;
    return 0;
}
