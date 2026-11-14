/* Private SH4-side command state; no installed address or live transport. */
#ifndef KOS_PRIVATE_MUSIC_CLIENT_H
#define KOS_PRIVATE_MUSIC_CLIENT_H
#include "aica_music_wire.h"
#define AICA_MC_SYNC 1u
#define AICA_MC_IDLE 2u
#define AICA_MC_WAIT 3u
#define AICA_MC_DONE 4u
#define AICA_MC_LOST 5u
#define AICA_MC_IO 1u
#define AICA_MC_PROTOCOL 2u
#define AICA_MC_EXHAUSTED 3u
typedef struct {
    /* Read native32-bit words at receipt word offset.0 success,negative error.
       Backend supplies real ordering; callback shims are not G2/cache proof. */
    int (*read)(void *,unsigned offset,uint32_t *words,unsigned count);
    int (*session)(void *,uint32_t *active);
    /* Copy all16 words before returning.1 admitted,0 definitely not admitted
       on THIS attempt,negative means admission may have happened. */
    int (*submit)(void *,const uint32_t words[16]);
    void *context;
} aica_music_client_ops_t;
typedef struct {
    aica_music_client_ops_t ops;
    uint32_t session,cookie,packet[16],superseded[16],generation[8];
    aica_music_receipt_t receipt;
    uint8_t state,error,admission_closed,uncertain,known_generation;
} aica_music_client_t;
/* First initialization only; requires exclusive fresh firmware owner. Poll SYNC
   until pristine ready receipt is observed. Never adopts an existing owner. */
int aica_music_client_init(aica_music_client_t *,uint32_t session,const aica_music_client_ops_t *);
/* Begin only from IDLE. Caller owns all reservations/queued bank references
   before this call.1 submitted,0 retained for retry/poll,-1 invalid/LOST.
   Even definitely-unsubmitted attempts remain WAIT until retry or quiesce;
   no implicit abandon or reference release is performed. */
int aica_music_client_begin(aica_music_client_t *,unsigned operation,unsigned port,
                            uint32_t generation,const uint32_t arguments[4]);
/* Exact retry, same cookie and bytes. A timeout never permits freeing state. */
int aica_music_client_retry(aica_music_client_t *);
/* Urgent quiesce may replace WAIT; preserves entire prior packet in superseded.
   Repeating during quiesce retries the SAME packet. Admission closes locally
   even if transport is uncertain. DONE must be consumed first. */
int aica_music_client_quiesce(aica_music_client_t *);
/*1 SYNC-ready or terminal DONE,0 pending/read failure,-1 LOST/invalid state.
   DONE exposes receipt and any superseded packet until explicitly consumed.
   At most three reads: sequence,64-word body,sequence. No busy wait/deadline. */
int aica_music_client_poll(aica_music_client_t *);
int aica_music_client_consume(aica_music_client_t *);
/* Serialized non-reentrant owner only. Caller-held storage/reference lifetimes
   extend through uncertain/pending/LOST state. This module frees NOTHING.
   MESSAGE receipt means FIFO admission, not playback. Close/quiesce receipt
   does not retire physical sample fetches or authorize pool/sample reuse. */
#endif
