/* KallistiOS ##version##

   vmu_bank_protocol.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vmu_bank_protocol.h"

uint32_t vmu_bank_command_encode(vmu_bank_command_t command, uint8_t value) {
    switch(command) {
    case VMU_BANK_COMMAND_INFO:
        return 0;
    case VMU_BANK_COMMAND_SELECT:
        return UINT32_C(0x00ff0000) | (uint32_t)value << 24;
    case VMU_BANK_COMMAND_LOCK:
        return UINT32_C(0x000000ff) |
               (value ? UINT32_C(0x0000ff00) : 0);
    default:
        errno = EINVAL;
        return 0;
    }
}

int vmu_bank_info_decode(const void *payload, size_t payload_size,
                         uint8_t *bank_count, uint8_t *current_bank,
                         bool *locked) {
    const uint8_t *bytes = payload;

    if(!payload || payload_size < sizeof(uint32_t) || !bank_count ||
       !current_bank || !locked || bytes[2] == UINT8_MAX ||
       bytes[3] > bytes[2]) {
        errno = EPROTO;
        return -1;
    }

    *locked = bytes[1] != 0;
    *bank_count = (uint8_t)(bytes[2] + 1u);
    *current_bank = bytes[3];
    return 0;
}

int vmu_bank_command_response_validate(vmu_bank_command_t command,
                                       uint8_t value, const void *payload,
                                       size_t payload_size) {
    const uint8_t *bytes = payload;

    if(!payload || payload_size < sizeof(uint32_t)) {
        errno = EPROTO;
        return -1;
    }
    if((command == VMU_BANK_COMMAND_SELECT && bytes[3] != value) ||
       (command == VMU_BANK_COMMAND_LOCK &&
        (bytes[1] != 0) != (value != 0)) ||
       command > VMU_BANK_COMMAND_LOCK) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}
