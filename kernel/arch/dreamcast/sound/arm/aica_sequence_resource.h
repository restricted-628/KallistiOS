#ifndef AICA_PRIVATE_SEQUENCE_RESOURCE_H
#define AICA_PRIVATE_SEQUENCE_RESOURCE_H
#include "wire.h"
#include "live.h"
#include "aica_music_live.h"
#include <dc/sound/aica_sequence_resource.h>
typedef struct {
    asq_catalog_t catalog;
    asq_wire_t wire;
    asql_t control;
} asq_resource_runtime_t;
void asq_resource_runtime_init(asq_resource_runtime_t *,volatile asq_receipt_t *);
void asq_resource_control_init(asq_resource_runtime_t *,aica_music_live_t *,aica_bank_registry_t *,volatile asql_receipt_t *);
_Static_assert(AICA_MEM_MUSIC_LIMIT==AICA_MEM_SEQUENCE_STATE,"music/sequence boundary");
_Static_assert(AICA_MEM_MUSIC_RECEIPT_END==AICA_MEM_SEQUENCE_RECEIPT,"receipt adjacency");
_Static_assert(AICA_MEM_SEQUENCE_RECEIPT_END<=0x2a000u,"receipts before private state");
_Static_assert(AICA_MEM_SEQUENCE_RECEIPT_END==AICA_MEM_SEQUENCE_CONTROL_RECEIPT,"sequence receipts adjacent");
_Static_assert(sizeof(asql_receipt_t)==AICA_MEM_SEQUENCE_CONTROL_RECEIPT_END-AICA_MEM_SEQUENCE_CONTROL_RECEIPT,"control receipt extent");
_Static_assert(AICA_MEM_SEQUENCE_CONTROL_RECEIPT_END<=AICA_MEM_MUSIC_STATE,"control receipt before state");
_Static_assert(sizeof(asq_receipt_t)==AICA_MEM_SEQUENCE_RECEIPT_END-AICA_MEM_SEQUENCE_RECEIPT,"receipt extent");
_Static_assert(AICA_MEM_SEQUENCE_STATE+sizeof(asq_resource_runtime_t)<=AICA_MEM_SEQUENCE_LIMIT,"sequence state extent");
#if defined(__arm__)
_Static_assert(AICA_MEM_MUSIC_STATE+sizeof(aica_music_live_t)<=AICA_MEM_SEQUENCE_STATE,"live state disjoint");
_Static_assert(AICA_MEM_SEQUENCE_STATE%_Alignof(asq_resource_runtime_t)==0,"sequence state alignment");
#endif
#endif
