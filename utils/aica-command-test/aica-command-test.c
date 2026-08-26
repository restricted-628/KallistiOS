/* KallistiOS ##version##

   Host-side AICA shared-command layout tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The shared header normally receives these fixed-width aliases from the
   target architecture header. Supply the exact wire widths on the host. */
#define __ARCH_TYPES_H
typedef uint8_t uint8;
typedef uint32_t uint32;

#include <dc/sound/aica_comm.h>

_Static_assert(sizeof(aica_cmd_t) == 32, "AICA command header wire size");
_Static_assert(sizeof(aica_channel_t) == 64, "AICA channel payload wire size");
_Static_assert(sizeof(aica_channel_mask_t) == 8,
               "AICA channel mask wire size");
_Static_assert(AICA_CMDSTR_CHANNEL_SIZE == 24,
               "AICA channel command word count");
_Static_assert(AICA_CMDSTR_CHANNEL_MASK_SIZE == 10,
               "AICA mask command word count");

int main(void) {
    const uint64_t channels = UINT64_C(0x8000000180000001);
    AICA_CMDSTR_CHANNEL_MASK(words, command, mask);

    memset(words, 0, sizeof(words));
    command->size = AICA_CMDSTR_CHANNEL_MASK_SIZE;
    command->cmd = AICA_CMD_SYNC_CHANNELS;
    mask->low = (uint32_t)channels;
    mask->high = (uint32_t)(channels >> 32);

    assert(command->size == 10);
    assert(command->cmd == AICA_CMD_SYNC_CHANNELS);
    assert(mask->low == UINT32_C(0x80000001));
    assert(mask->high == UINT32_C(0x80000001));

    puts("AICA shared-command layout tests passed");
    return 0;
}
