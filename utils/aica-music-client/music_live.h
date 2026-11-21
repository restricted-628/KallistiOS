/* Explicit private KOS owner. Caller serializes calls and sound lifetime. */
#ifndef KOS_PRIVATE_MUSIC_LIVE_HOST_H
#define KOS_PRIVATE_MUSIC_LIVE_HOST_H
#include "music_client.h"
#include "abk_registry.h"
#include <dc/sound/port.h>
typedef struct {
    aica_music_client_t client;
    snd_port_t ports[64];
    abk_bank_t *pending_bank;
    uint32_t masks[2], session, timeout_ms;
    unsigned count;
    uint8_t initialized, claim_attempted, retained;
} aica_music_owner_t;
/* Initialize zeroed storage, once per fresh exclusively owned sound runtime.
   Does not initialize/reset sound. No legacy/raw player may use the pool.
   0 means initialized; call poll/retry to complete CLAIM. Error after possible
   admission retains storage/ports; there is intentionally no destroy/free API.
   All caller objects and immutable bank storage outlive WAIT/LOST/QUIESCED. */
int aica_music_owner_init(aica_music_owner_t *, unsigned voices, uint32_t timeout_ms);
int aica_music_owner_poll(aica_music_owner_t *);
int aica_music_owner_retry(aica_music_owner_t *);
/* Requires client IDLE. Bank pending reference is acquired before OPEN enqueue;
   exact terminal ACK transfers protection to ARM producer ownership. */
int aica_music_owner_open(aica_music_owner_t *, unsigned port, abk_bank_t *);
/* CLAIM and OPEN only through init/open. Other private wire operations retain
   existing client rules. MESSAGE result is queue admission, not audible output. */
int aica_music_owner_command(aica_music_owner_t *, unsigned operation, unsigned port,
    uint32_t generation, const uint32_t arguments[4]);
/* Optional bounded AICA MIDI FIFO receive into a direct, open music port.
   Enable begins at an empty FIFO boundary. Poll/consume the mode receipt.
   Running status and channel voice input are supported. The source must
   fit the service cadence. Port reuse, protection and faults disable input. */
int aica_music_owner_set_input_mode(aica_music_owner_t *, unsigned port,
    uint32_t generation, unsigned enabled);
int aica_music_owner_quiesce(aica_music_owner_t *);
int aica_music_owner_consume(aica_music_owner_t *);
/* No natural completion in this live slice. Logical QUIESCE never frees ports,
   bank voice leases or samples; physical retirement remains unqualified. */
#endif
