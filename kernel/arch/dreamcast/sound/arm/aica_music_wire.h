/* Private candidate protocol. Not installed, selected, or a stable public ABI. */
#ifndef AICA_MUSIC_WIRE_H
#define AICA_MUSIC_WIRE_H
#include "aica_music.h"
#define AICA_MUSIC_WIRE_COMMAND 0x0du
#define AICA_MUSIC_WIRE_VERSION 1u
#define AICA_MUSIC_WIRE_PENDING 1
#define AICA_MUSIC_WIRE_CLAIM 1u
#define AICA_MUSIC_WIRE_OPEN 2u
#define AICA_MUSIC_WIRE_CLOSE 3u
#define AICA_MUSIC_WIRE_MESSAGE 4u
#define AICA_MUSIC_WIRE_MUTE 5u
#define AICA_MUSIC_WIRE_GM 6u
#define AICA_MUSIC_WIRE_RELEASE 7u
#define AICA_MUSIC_WIRE_PARAMETER 8u
#define AICA_MUSIC_WIRE_RESET 9u
#define AICA_MUSIC_WIRE_QUERY 10u
#define AICA_MUSIC_WIRE_QUIESCE 11u
#define AICA_MUSIC_WIRE_RESET_ALL 12u
#define AICA_MUSIC_WIRE_CLOSE_ALL 13u
/* Payload words: session, operation, port, generation, four operation arguments.
   Packet timestamp/misc are zero, size16 words, cmd_id is a nonzero increasing
   cookie. One pending host request globally; retries must be byte-identical. */
typedef struct {
    uint32_t sequence, version, ready, session, cookie, operation;
    int32_t result;
    uint32_t flags, request[8]; /* flags1 claimed,2 quiescing,4 faulted; bits8..15 bank pins */
    uint32_t generation,bank,bank_generation,open,mute,gm,release_mode,mailbox;
    int32_t volume,pan,pitch,direct,effect;
    uint32_t fade_remaining[3],queued,applied,rejected,declined,stolen;
    int32_t last_result;
    uint32_t active,held,sustained,releasing,token,cancelled_cookie,updates_pending,reserved[19];
} aica_music_receipt_t;
_Static_assert(sizeof(aica_music_receipt_t)==256,"private music receipt");
typedef struct {
    /* claim atomically establishes an exclusive physical pool, or returns0
       without acquiring it. Pool release is outside this resident-only slice. */
    int (*claim)(void *,uint32_t low,uint32_t high);
    int (*pin_bank)(void *,uint32_t session,uint32_t bank,uint32_t generation);
    int (*unpin_bank)(void *,uint32_t session,uint32_t bank,uint32_t generation);
    void *context;
} aica_music_wire_ops_t;
typedef struct {
    aica_music_t *music;
    volatile aica_music_receipt_t *receipt;
    aica_music_wire_ops_t ops;
    uint32_t session,cookie,cancelled_cookie,request[8];
    uint8_t claimed,pending,faulted,bank_pins;
} aica_music_wire_t;
/* First initialization only. Memory placement/firmware activation and host
   ownership are integration gates; this core assigns no absolute RAM address. */
int aica_music_wire_init(aica_music_wire_t *,aica_music_t *,
    volatile aica_music_receipt_t *,const aica_music_wire_ops_t *);
/* 1 admitted/replayed,0 malformed/stale/conflicting request. Result is in the
   coherent receipt. MESSAGE acknowledges FIFO admission, not voice execution.
   CLOSE skips earlier MIDI work; QUIESCE also cancels a pending control and
   records its cookie. Other controls wait for earlier MIDI work to drain. */
int aica_music_wire_command(aica_music_wire_t *,const aica_cmd_t *,uint32_t active_session);
/* Call after bounded MIDI service. Completes at most one pending control. */
void aica_music_wire_service(aica_music_wire_t *);
#endif
