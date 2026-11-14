/* Explicit private KOS sequence-resource owner, no automatic sound reset. */
#ifndef KOS_PRIVATE_ASQ_OWNER_H
#define KOS_PRIVATE_ASQ_OWNER_H
#include "client.h"
#include <dc/spu.h>
#define ASQH_EMPTY 0u
#define ASQH_UPLOAD 1u
#define ASQH_REGISTER 2u
#define ASQH_READY 3u
#define ASQH_DROP 4u
#define ASQH_FAILED 5u
#define ASQH_CLOSED 6u
#define ASQH_LOST 7u
#define ASQH_QUERY 8u
typedef struct {
    spu_transfer_request_t *transfer;
    const uint8_t *input;
    uint32_t state,generation,requested_generation,source,events,bytes,capacity;
    uint32_t committed,chunk;
    int error;
    uint8_t close_requested,safe_to_free,admitted,reserved;
} asq_host_slot_t;
typedef struct {
    asq_client_t client;
    asq_host_slot_t slots[8];
    uint32_t session,timeout_ms,chunk_limit,active;
    uint8_t initialized,lost,reserved[2];
} asq_owner_t;
/* Zeroed, caller-owned, aligned storage. One exclusive serialized ordinary-thread
   owner per live sound runtime. Context and input spans must not alias. Input
   immutable/held through final SPU request destruction; ARAM source AND decoded
   storage remain owned until exact successful DROP (or definite non-admission).
   No reset/shutdown/raw catalog producer may race this owner. */
int asq_owner_init(asq_owner_t *,uint32_t chunk_limit,uint32_t timeout_ms);
/* EMPTY/CLOSED slot only. Any allocation/admission attempt remains represented
   even on error: call close/pump; never discard the context on a failed begin. */
int asq_owner_begin(asq_owner_t *,unsigned slot,const void *source,size_t bytes);
/* At most one transfer status/destroy/next submit or one receipt poll per call.
   0 progress/settled, -1 observable error. EBUSY never releases storage. */
int asq_owner_pump(asq_owner_t *);
int asq_owner_retry(asq_owner_t *);
/* Query an admitted resource; snapshot remains in client.receipt when settled.
   No query may overtake uncertain publication or another active operation. */
int asq_owner_query(asq_owner_t *,unsigned slot);
/* Latch close/cancel. While uploading, cancel and retire before freeing. While
   registration is uncertain, resolve exact admission before sending DROP. Busy
   core references retain both allocations. Inspect state==CLOSED for completion.
   NOT_ADMITTED frees ONLY this request's exclusive never-admitted allocations,
   never existing storage described by a conflicting slot's receipt. */
int asq_owner_close(asq_owner_t *,unsigned slot);
/* LOST retains ARAM allocations; pump may still retire a live transfer. No force
   close or reinitialization API. Source/event CPU retirement is not voice/DSP or
   physical PCM retirement. This slice publishes ASQ but does not start music. */
#endif
