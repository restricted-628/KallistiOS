#ifndef __AICA_BANK_REGISTRY_H
#define __AICA_BANK_REGISTRY_H
#include <dc/sound/aica_bank_registry.h>
typedef struct aica_bank_registry {
    volatile aica_bank_header_t *header;
    volatile aica_bank_record_t *records;
    /* Called only after checking the complete retail RAM extent. */
    const uint8_t *(*map)(uint32_t address, uint32_t bytes);
    uint32_t cursor[AICA_BANK_SLOTS], end[AICA_BANK_SLOTS];
    uint32_t address[AICA_BANK_SLOTS], bytes[AICA_BANK_SLOTS];
    uint32_t samples[AICA_BANK_SLOTS], zones[AICA_BANK_SLOTS];
    uint16_t next[AICA_BANK_SLOTS], head, tail;
} aica_bank_registry_t;
void aica_bank_registry_init(aica_bank_registry_t *b);
int aica_bank_registry_command(aica_bank_registry_t *b, const aica_cmd_t *p,
                               uint32_t active_session);
/* Round robin: <=32 metadata records TOTAL, independent of active-bank count.
   With N validating banks, each gets a record in <=ceil(N/32) passes. */
unsigned aica_bank_registry_service(aica_bank_registry_t *b);
/* Original future-consumer hooks. No voice starts here. A voice unpin requires
   the caller's independently qualified final-fetch endpoint; stop is not one. */
int aica_bank_registry_pin(aica_bank_registry_t *b, uint32_t session,
                           uint32_t slot, uint32_t generation, unsigned kind);
int aica_bank_registry_unpin(aica_bank_registry_t *b, uint32_t session,
                             uint32_t slot, uint32_t generation, unsigned kind);
/* Atomic queued->voice handoff, no zero-reference interval. */
int aica_bank_registry_promote(aica_bank_registry_t *b, uint32_t session,
                               uint32_t slot, uint32_t generation);
#endif
