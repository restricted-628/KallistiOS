/* Original caller-backed ASQ ownership, private to the native utility. */
#ifndef KOS_PRIVATE_ASQ_CATALOG_H
#define KOS_PRIVATE_ASQ_CATALOG_H
#include "asq.h"
#define ASQ_CATALOG_SLOTS 8u
#define ASQ_CATALOG_BORROWS 16u
#define ASQ_CATALOG_INVALID -1
#define ASQ_CATALOG_BUSY -2
#define ASQ_CATALOG_FAULT -3
#define ASQ_CATALOG_EMPTY 0u
#define ASQ_CATALOG_PREPARING 1u
#define ASQ_CATALOG_READY 2u
#define ASQ_CATALOG_FAILED 3u
typedef struct {
    aica_seq_validation_t validation;
    uint32_t slot, generation;
} asq_lease_t;
typedef struct {
    asq_bank_t bank;
    uint32_t generation, playing, borrowed, state;
} asq_resource_t;
typedef struct {
    asq_resource_t slots[ASQ_CATALOG_SLOTS];
    asq_lease_t *borrowers[ASQ_CATALOG_BORROWS];
    uint32_t faulted;
} asq_catalog_t;
/* One serialized owner, no reentrant callbacks. Contexts are aligned and live.
   Init only before first use, never as a way to discard outstanding references.
   No allocation, transfer, sound RAM address, or physical sample ownership. */
int asq_catalog_init(asq_catalog_t *);
/* Only EMPTY slots; generation must equal the previous generation + 1 without
   wrapping. Source and used event storage must remain held and disjoint from
   the catalog, other occupied resources and outstanding leases until drop.
   Source is immutable throughout; events become immutable after READY.
   Begin errors leave catalog/storage unchanged. */
int asq_catalog_begin(asq_catalog_t *,unsigned slot,uint32_t generation,
    const void *source,size_t bytes,aica_seq_event_t *storage,size_t capacity);
/* Same work bound as asq_step; 1 whole bank READY, 0 pending, negative failure. */
int asq_catalog_step(asq_catalog_t *,unsigned slot,uint32_t generation);
/* Cancel preparation/failure or unload READY. BUSY leaves state unchanged while
   a validation lease or executing/paused/chained program retains this slot.
   Success clears pointers, preserves the generation and retires only CPU users. */
int asq_catalog_drop(asq_catalog_t *,unsigned slot,uint32_t generation);
/* Lease is registered by its exact address: do not move, copy or modify it.
   Keep it held through the synchronous core play/chain call. The core's retain
   callback then owns an independent execution reference. No borrowed validation
   may be used after return, even if its bytes were copied elsewhere. */
int asq_catalog_acquire(asq_catalog_t *,unsigned slot,uint32_t generation,
    unsigned program,asq_lease_t *);
int asq_catalog_return(asq_catalog_t *,asq_lease_t *);
/* Compatible with aica_seq_ops_t and aica_music_sequences_init program hooks.
   Retain requires an exact descriptor from a current validation lease. Release
   is only for a corresponding successful core retain, exactly once. Invalid
   callback release latches a fault and prevents subsequent reclamation. */
int asq_catalog_retain(void *,const aica_seq_program_t *);
void asq_catalog_release(void *,const aica_seq_program_t *);
#endif
