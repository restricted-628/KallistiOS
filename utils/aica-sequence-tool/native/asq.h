/* Original private sequence container; no SDK wire compatibility. */
#ifndef KOS_PRIVATE_ASQ_H
#define KOS_PRIVATE_ASQ_H
#include <stddef.h>
#include <stdint.h>
#include "aica_sequence.h"
#define ASQ_MAX_PROGRAMS 128u
#define ASQ_MAX_EVENTS 65536u
#define ASQ_CRC_BUDGET 512u
/* Planning only: does not establish CRC or event validity. */
typedef struct {
    uint32_t size, programs, events, event_offset;
} asq_layout_t;
typedef struct {
    const uint8_t *source;
    aica_seq_event_t *events;
    asq_layout_t layout;
    aica_seq_validation_t validation;
    uint32_t crc, crc_offset, program, first, count, decoded;
    uint8_t phase; /* private: 1 CRC,2 directory,3 decode,4 grammar,5 ready,6 failed */
} asq_bank_t;
/* Source may be unaligned; result must not alias it. Unchanged on failure. */
int asq_measure(const void *source,size_t bytes,asq_layout_t *result);
/* Context and native event storage must be aligned, mutually disjoint and
   disjoint from source. Source is immutable through all step/get calls; events
   become immutable after ready, and remain held through every sequence pin.
   Invalid begin leaves context/storage unchanged. After successful begin,
   failure may leave partial decoded events: never expose them as ready. */
int asq_begin(asq_bank_t *,const void *source,size_t bytes,
              aica_seq_event_t *storage,size_t capacity);
/* 1 ready,0 pending,-1 invalid. Each call CRCs<=512 bytes OR decodes<=32
   events OR checks<=32 events, plus at most one fixed directory record. */
int asq_step(asq_bank_t *);
/* Produces a core-ready validation only after the complete bank passes.
   Result must not alias bank/source/storage. No ownership/pin is acquired. */
int asq_get(const asq_bank_t *,unsigned program,aica_seq_validation_t *result);
#endif
