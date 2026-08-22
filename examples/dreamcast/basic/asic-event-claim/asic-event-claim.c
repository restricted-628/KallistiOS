/* KallistiOS ##version##

   asic-event-claim.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/asic.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static volatile uint32_t event_count;

static void event_handler(uint32_t code, void *data) {
    volatile uint32_t *count = data;

    if(code == ASIC_EVT_GD_DMA_ILLADDR)
        ++*count;
}

int main(int argc, char **argv) {
    asic_evt_claim_t claim = ASIC_EVT_CLAIM_INVALID;
    asic_evt_status_t status;
    int failed = 0;

    (void)argc;
    (void)argv;

    if(asic_evt_claim(ASIC_EVT_GD_DMA_ILLADDR, ASIC_IRQB, event_handler,
                      (void *)&event_count, &claim) < 0) {
        printf("ASIC-EVENT-CLAIM: %s errno=%d\n",
               errno == EBUSY ? "BUSY" : "FAIL", errno);
        return EXIT_FAILURE;
    }

    if(asic_evt_get_status(ASIC_EVT_GD_DMA_ILLADDR, &status) < 0 ||
       !status.handler_present || !status.exclusively_claimed ||
       status.enabled_levels != (UINT8_C(1) << ASIC_IRQB) ||
       status.dispatches != 0)
        failed = 1;

    if(asic_evt_claim_mask(claim) < 0 ||
       asic_evt_get_status(ASIC_EVT_GD_DMA_ILLADDR, &status) < 0 ||
       status.enabled_levels != 0 ||
       asic_evt_claim_unmask(claim) < 0)
        failed = 1;

    if(asic_evt_release(claim) < 0)
        failed = 1;

    printf("ASIC-EVENT-CLAIM: %s events=%lu\n",
           failed ? "FAIL" : "PASS", (unsigned long)event_count);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
