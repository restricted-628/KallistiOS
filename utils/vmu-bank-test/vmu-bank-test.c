/* KallistiOS ##version##

   vmu-bank-test.c
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vmu_bank_protocol.h"

int main(void) {
    static const uint8_t info_payload[] = {0, 0xff, 3, 2};
    static const uint8_t select_payload[] = {0, 0, 0xff, 1};
    static const uint8_t unlock_payload[] = {0xff, 0, 0, 0};
    uint8_t bank_count = 0, current_bank = 0;
    bool locked = false;

    if(vmu_bank_command_encode(VMU_BANK_COMMAND_INFO, 0) != 0 ||
       vmu_bank_command_encode(VMU_BANK_COMMAND_SELECT, 2) !=
           UINT32_C(0x02ff0000) ||
       vmu_bank_command_encode(VMU_BANK_COMMAND_LOCK, 1) !=
           UINT32_C(0x0000ffff) ||
       vmu_bank_command_encode(VMU_BANK_COMMAND_LOCK, 0) !=
           UINT32_C(0x000000ff) ||
       vmu_bank_info_decode(info_payload, sizeof(info_payload),
                            &bank_count, &current_bank, &locked) < 0 ||
       bank_count != 4 || current_bank != 2 || !locked ||
       vmu_bank_command_response_validate(
           VMU_BANK_COMMAND_SELECT, 1, select_payload,
           sizeof(select_payload)) < 0 ||
       vmu_bank_command_response_validate(
           VMU_BANK_COMMAND_LOCK, 0, unlock_payload,
           sizeof(unlock_payload)) < 0) {
        fprintf(stderr, "vmu-bank-test: FAIL errno=%d\n", errno);
        return 1;
    }

    errno = 0;
    if(vmu_bank_command_response_validate(
           VMU_BANK_COMMAND_SELECT, 2, select_payload,
           sizeof(select_payload)) == 0 || errno != EPROTO) {
        fprintf(stderr, "vmu-bank-test: FAIL mismatch errno=%d\n", errno);
        return 1;
    }

    puts("vmu-bank-test: PASS");
    return 0;
}
