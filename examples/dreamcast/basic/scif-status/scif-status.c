/* KallistiOS ##version##

   scif-status.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>

#include <inttypes.h>
#include <stdio.h>

static const char *parity_name(scif_parity_t parity) {
    switch(parity) {
        case SCIF_PARITY_NONE:
            return "none";
        case SCIF_PARITY_EVEN:
            return "even";
        case SCIF_PARITY_ODD:
            return "odd";
        default:
            return "invalid";
    }
}

int main(void) {
    scif_status_t status;

    printf("KallistiOS ##version##\n\n");

    if(scif_get_status(&status) < 0) {
        perror("scif_get_status");
        return 1;
    }

    printf("Byte I/O: %s, receive IRQ: %s\n",
           status.enabled ? "available" : "unavailable",
           status.irq_enabled ? "enabled" : "disabled");
    printf("Format: %" PRIu32 " requested, %" PRIu32
           " actual (%" PRId32 " ppm), %u data, %u stop, %s parity\n",
           status.config.baud, status.actual_baud, status.baud_error_ppm,
           status.config.data_bits, status.config.stop_bits,
           parity_name(status.config.parity));
    printf("Flow: %s, RX trigger: %u, TX trigger: %u\n",
           status.config.flow_control == SCIF_FLOW_HARDWARE ?
           "RTS/CTS" : "none",
           status.config.rx_trigger, status.config.tx_trigger);
    printf("FIFO: %zu receive, %zu transmit\n",
           status.receive_queued, status.transmit_queued);
    printf("Events #%" PRIu32 ": drop=%" PRIu32 ", frame=%" PRIu32
           ", parity=%" PRIu32 ", overrun=%" PRIu32
           ", break=%" PRIu32 ", timeout=%" PRIu32 "\n",
           status.event_sequence, status.receive_dropped,
           status.framing_errors, status.parity_errors,
           status.overrun_errors, status.breaks, status.transmit_timeouts);

    return 0;
}
