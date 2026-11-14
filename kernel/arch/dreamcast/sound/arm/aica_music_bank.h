/* Original ABK v1 adapter; ARM-owner serialized, no allocation or MMIO. */
#ifndef AICA_MUSIC_BANK_H
#define AICA_MUSIC_BANK_H
#include "aica_music.h"
#include "aica_bank_registry.h"
#define AICA_MUSIC_ZONE_BUDGET 32u
typedef struct {
    aica_bank_registry_t *registry;
    uint32_t session;
    /* Each set bit owns one physical voice reference. No logical operation
       clears it. Generation cannot change while any such reference exists. */
    uint32_t leases[AICA_BANK_SLOTS][2], generation[AICA_BANK_SLOTS];
    aica_music_event_t pending;
    uint32_t cursor;
    uint8_t pinned, bank, program, drum;
} aica_music_bank_t;
/* Before first use only; never reinitialize a live adapter to clear leases. */
void aica_music_bank_init(aica_music_bank_t *,aica_bank_registry_t *,uint32_t session);
/* <=32 zone records plus one sample record per invocation. A RETRY owns a
   producer pin; cancel after rejection or after retain has acquired the voice
   pin. No zero-reference interval. ABK v1 has one non-overlapping zone. */
int aica_music_bank_resolve(void *,const aica_music_event_t *,unsigned bank,
                            unsigned program,unsigned drum,aica_music_tone_t *);
void aica_music_bank_cancel(void *);
int aica_music_bank_retain(void *,unsigned voice,const aica_music_tone_t *);
#endif
