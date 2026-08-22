/* KallistiOS ##version##

   Expansion capability probe example
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <dc/expansion.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *device_name(expansion_device_type_t type) {
    switch(type) {
        case EXPANSION_DEVICE_BROADBAND:
            return "PCI Ethernet adapter";
        case EXPANSION_DEVICE_LAN:
            return "8-bit Ethernet adapter";
        case EXPANSION_DEVICE_MODEM:
            return "modem";
        case EXPANSION_DEVICE_UNKNOWN_8BIT:
            return "active unclassified 8-bit device";
        case EXPANSION_DEVICE_UNKNOWN_PCI:
            return "active unclassified PCI device";
        case EXPANSION_DEVICE_NONE:
            return "none identified";
    }

    return "invalid";
}

int main(int argc, char **argv) {
    expansion_status_t status;
    uint32_t flags = EXPANSION_PROBE_DEFAULT;

    if(argc > 1 && !strcmp(argv[1], "--reset-8bit"))
        flags |= EXPANSION_PROBE_RESET_8BIT;

    if(expansion_probe(&status, flags) < 0) {
        printf("Expansion probe failed: %s\n", strerror(errno));
        return 1;
    }

    printf("Device:       %s\n", device_name(status.type));
    printf("Present:      %s\n", status.present ? "yes" : "no");
    printf("Active:       %s\n", status.active ? "yes" : "no");
    printf("Complete:     %s\n", status.complete ? "yes" : "no");
    printf("Reset used:   %s\n", status.reset_performed ? "yes" : "no");
    printf("Capabilities: 0x%08" PRIx32 "\n", status.capabilities);
    printf("Maximum rate: %" PRIu32 " bps\n", status.maximum_bps);
    printf("Sequence:     %" PRIu32 "\n", status.sequence);

    if(!status.complete)
        puts("Inactive 8-bit detection was not requested.");

    return 0;
}
