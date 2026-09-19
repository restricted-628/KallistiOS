/* KallistiOS ##version##

   vmu_bank_protocol.h
   Copyright (C) 2026 Joseph Black

*/

#ifndef __DC_VMU_BANK_PROTOCOL_H
#define __DC_VMU_BANK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum vmu_bank_command {
    VMU_BANK_COMMAND_INFO,
    VMU_BANK_COMMAND_SELECT,
    VMU_BANK_COMMAND_LOCK
} vmu_bank_command_t;

uint32_t vmu_bank_command_encode(vmu_bank_command_t command, uint8_t value);
int vmu_bank_info_decode(const void *payload, size_t payload_size,
                         uint8_t *bank_count, uint8_t *current_bank,
                         bool *locked);
int vmu_bank_command_response_validate(vmu_bank_command_t command,
                                       uint8_t value, const void *payload,
                                       size_t payload_size);

#endif /* __DC_VMU_BANK_PROTOCOL_H */
