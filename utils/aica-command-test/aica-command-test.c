/* KallistiOS ##version##

   Host-side AICA shared-command layout tests.
   Copyright (C) 2026 Joseph Black
*/

#include <assert.h>
#include <stddef.h>
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
_Static_assert(sizeof(aica_driver_info_t) == 48,
               "AICA driver information wire size");
_Static_assert(AICA_CMDSTR_DRIVER_INFO_SIZE == 20,
               "AICA driver information word count");
_Static_assert(sizeof(aica_channel_config_t) == 148,
               "checked channel configuration wire size");
_Static_assert(sizeof(aica_channel_control_t) == 160,
               "checked channel command payload wire size");
_Static_assert(sizeof(aica_channel_status_ext_t) == 164,
               "checked channel status wire size");
_Static_assert(AICA_CMDSTR_CHANNEL_CONTROL_SIZE == 48,
               "checked channel command word count");
_Static_assert(offsetof(aica_channel_control_t, config) == 12,
               "checked channel payload prefix");
_Static_assert(offsetof(aica_channel_status_ext_t, config) == 16,
               "checked channel status prefix");
_Static_assert(offsetof(aica_channel_config_t, filter_level) == 112,
               "checked channel filter level offset");
_Static_assert(offsetof(aica_channel_config_t, filter_release_rate) == 144,
               "checked channel configuration tail offset");

static void test_channel_control(void) {
    AICA_CMDSTR_CHANNEL_CONTROL(words, command, control);

    memset(words, 0, sizeof(words));
    command->size = AICA_CMDSTR_CHANNEL_CONTROL_SIZE;
    command->cmd = AICA_CMD_CHANNEL_CONTROL;
    command->cmd_id = 63;
    control->operation = AICA_CHANNEL_OP_UPDATE;
    control->fields = AICA_CHANNEL_UPDATE_ENVELOPE |
                      AICA_CHANNEL_UPDATE_FILTER |
                      AICA_CHANNEL_UPDATE_ROUTING;
    control->config.attack_rate = 31;
    control->config.release_rate = 19;
    control->config.effect_channel = 15;
    control->config.effect_send = 12;
    control->config.direct_level = 9;
    control->config.filter_enabled = 1;
    control->config.filter_level[4] = 0x1fff;

    assert(command->size == 48);
    assert(command->cmd_id == 63);
    assert(control->operation == AICA_CHANNEL_OP_UPDATE);
    assert(control->fields == 0x68);
    assert(control->config.release_rate == 19);
    assert(control->config.filter_level[4] == 0x1fff);
}

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

    test_channel_control();

    puts("AICA shared-command layout tests passed");
    return 0;
}
