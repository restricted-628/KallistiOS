/* Serialized ARM owner. No natural completion or physical lease release. */
#ifndef AICA_PRIVATE_MUSIC_LIVE_H
#define AICA_PRIVATE_MUSIC_LIVE_H
#include "aica_music_wire.h"
#include "aica_music_bank.h"
#include "aica_music_sequence.h"
#include <dc/sound/aica_music_live.h>
typedef struct {
    uint32_t masks[2], start_token[64];
    uint32_t last_tick, remainder, session, failures;
    uint8_t physical[64], voices, claimed, clock_ready, quiesced;
} aica_music_backend_t;
typedef struct {
    aica_music_t music;
    aica_music_bank_t bank;
    aica_music_sequences_t sequences;
    aica_music_wire_t wire;
    aica_music_backend_t backend;
} aica_music_live_t;
/* These platform hooks are called only by the serialized ARM backend. Validate
   is side-effect free; invalidate preflights the ENTIRE mask before any write.
   Apply uses the admitted config and publishes current legacy channel status. */
int aica_music_hw_validate(unsigned channel, unsigned operation,
    const aica_channel_config_t *, uint32_t fields);
int aica_music_hw_invalidate(uint32_t low, uint32_t high);
void aica_music_hw_apply(unsigned channel, unsigned operation,
    const aica_channel_config_t *, uint32_t fields);
void aica_music_live_init(aica_music_live_t *, aica_bank_registry_t *,
    volatile aica_music_receipt_t *);
int aica_music_live_command(aica_music_live_t *, const aica_cmd_t *, uint32_t session);
void aica_music_live_service(aica_music_live_t *, uint32_t tick);
int aica_music_live_owned(const aica_music_live_t *, unsigned channel);
int aica_music_live_intersects(const aica_music_live_t *, uint32_t low, uint32_t high);
uint32_t aica_music_clock_step(aica_music_backend_t *, uint32_t tick);
void aica_music_clock_rebase(aica_music_backend_t *);
/* Sole firmware caller uses10. Requires delay<UINT32_MAX and observation before
   a full counter wrap; preserves the prior delay+1 strict exit convention. */
static inline int aica_timer_waiting(uint32_t start, uint32_t now, uint32_t delay) {
    return (uint32_t)(now - start) <= delay;
}
_Static_assert(sizeof(aica_music_backend_t)==348, "backend layout");
_Static_assert(AICA_MEM_BANK_END==AICA_MEM_MUSIC_RECEIPT, "receipt follows banks");
_Static_assert(sizeof(aica_music_receipt_t)==256, "receipt size");
_Static_assert(AICA_MEM_MUSIC_RECEIPT_END<=AICA_MEM_MUSIC_STATE, "state separation");
_Static_assert(AICA_MEM_MUSIC_STATE+sizeof(aica_music_live_t)<=AICA_MEM_MUSIC_LIMIT, "state extent");
#if defined(__arm__)
_Static_assert(sizeof(aica_music_live_t)==19744, "complete ARM state");
_Static_assert(AICA_MEM_MUSIC_STATE % _Alignof(aica_music_live_t)==0, "state alignment");
#endif
#endif
