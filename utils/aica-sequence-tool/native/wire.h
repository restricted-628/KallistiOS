/* Private original resident-sequence protocol. No SDK wire compatibility. */
#ifndef KOS_PRIVATE_ASQ_WIRE_H
#define KOS_PRIVATE_ASQ_WIRE_H
#include "catalog.h"
#include <dc/sound/aica_comm.h>
#define ASQ_WIRE_COMMAND 0x0eu
#define ASQ_WIRE_VERSION 1u
#define ASQ_WIRE_PREPARE 1u
#define ASQ_WIRE_QUERY 2u
#define ASQ_WIRE_DROP 3u
#define ASQ_WIRE_PENDING 1
#define ASQ_WIRE_CANCELLED -4
#define ASQ_WIRE_ADMITTED 1u
#define ASQ_WIRE_FAULTED 2u
#define ASQ_WIRE_SUPERSEDED 4u
/* ADMITTED applies only to the primary PREPARE request. Its absence never
   authorizes freeing an occupied slot's storage. Only this request's exclusive,
   never-admitted provisional allocations may unwind on exact rejection. */
typedef struct {
    uint32_t sequence,version,ready,session,cookie,operation;
    int32_t result;
    uint32_t flags,request[8];
    uint32_t generation,state,borrowed,playing,source,source_bytes;
    uint32_t events,capacity,programs,event_count;
    uint32_t superseded_cookie;
    int32_t superseded_result;
    uint32_t superseded_request[8],reserved[28];
} asq_receipt_t;
_Static_assert(sizeof(asq_receipt_t)==256,"ASQ receipt");
typedef struct {
    asq_catalog_t *catalog;
    volatile asq_receipt_t *receipt;
    /* Called only after checked RAM bounds. Map retains no ownership itself;
       source is immutable, decoded storage writable only during preparation. */
    void *(*map)(void *,uint32_t address,uint32_t bytes,int write);
    void *context;
    uint32_t session,cookie,request[8],spans[ASQ_CATALOG_SLOTS][4];
    uint32_t superseded_cookie,superseded_request[8];
    int32_t result,superseded_result;
    uint8_t pending,faulted,admitted,reserved;
} asq_wire_t;
/* First initialization only; catalog already initialized, all objects disjoint.
   Serialized owner, no concurrent direct catalog mutations or raw producer. */
int asq_wire_init(asq_wire_t *,asq_catalog_t *,volatile asq_receipt_t *,
    void *(*map)(void *,uint32_t,uint32_t,int),void *context);
/* Packet16words: normal8-word header,session,op,slot,generation,4args.
   PREPARE args source,bytes,events,capacity; QUERY/DROP args zero.
   1 accepted/replayed,0 rejected without consuming the request identity. */
int asq_wire_command(asq_wire_t *,const aica_cmd_t *,uint32_t active_session);
/* One parser step total per call. No unsolicited per-step receipt publications. */
void asq_wire_service(asq_wire_t *);
#endif
