/* Original private sequence control transport; one serialized ARM owner. */
#ifndef KOS_PRIVATE_ASQ_LIVE_H
#define KOS_PRIVATE_ASQ_LIVE_H
#include "catalog.h"
#include "aica_music_sequence.h"
#include "aica_music_wire.h"
#define ASQL_COMMAND 0x0fu
#define ASQL_VERSION 1u
#define ASQL_BIND 1u
#define ASQL_UNBIND 2u
#define ASQL_PLAY 3u
#define ASQL_STOP 4u
#define ASQL_PAUSE 5u
#define ASQL_CONTINUE 6u
#define ASQL_CHAIN 7u
#define ASQL_LOOP_EXIT 8u
#define ASQL_SPEED 9u
#define ASQL_BEAT 10u
#define ASQL_RELEASE 11u
#define ASQL_QUERY 12u
#define ASQL_STOP_ALL 13u
#define ASQL_PENDING 1
#define ASQL_DECLINED 2
#define ASQL_INVALID -1
#define ASQL_BUSY -2
#define ASQL_FAULT -3
#define ASQL_CANCELLED -4
typedef struct {
    uint32_t sequence,version,ready,session,cookie,operation;
    int32_t result;
    uint32_t flags,request[8]; /* 1 initialized,2 faulted,4 superseded */
    uint32_t bound,state,generation,bank,bank_generation,slot,resource_generation,program;
    uint32_t chain_slot,chain_generation,chain_program,cursor,event_count,priority,tempo,speed_target;
    uint32_t ticks[2],beats[2],debt[2],remaining[2];
    uint32_t processed,notes,resuming,dropped,last_tick,remainder,cancelled_cookie;
    int32_t cancelled_result;
    uint32_t cancelled_request[8],speed_from,speed_elapsed,speed_duration,beat_multiply,beat_shift,reserved[3];
} asql_receipt_t;
_Static_assert(sizeof(asql_receipt_t)==256,"sequence control receipt");
typedef struct { uint32_t tick,remainder,paused_remainder; } asql_clock_t;
typedef struct {
    uint32_t slot,generation,program;
    const aica_seq_event_t *events;
} asql_program_t;
typedef struct {
    aica_music_sequences_t *adapter;
    aica_music_wire_t *music;
    aica_bank_registry_t *registry;
    asq_catalog_t *catalog;
    volatile asql_receipt_t *receipt;
    asql_clock_t clocks[8];
    asql_program_t current[8],chained[8];
    uint32_t session,cookie,request[8],cancelled_cookie,cancelled_request[8];
    int32_t result,cancelled_result;
    uint8_t initialized,pending,faulted,next_port;
} asql_t;
/* Before first use only; adapter storage is already reserved but uninitialized.
   No caller may directly change bound ports, sequences, clocks or catalog refs. */
int asql_init(asql_t *,aica_music_sequences_t *,aica_music_wire_t *,
              aica_bank_registry_t *,asq_catalog_t *,volatile asql_receipt_t *);
/* All packets16words; payload session/op/port/music-generation/4args. PLAY args
   slot/resource-generation/program/priority; CHAIN same with priority0. SPEED
   signed-relative/fade-ms; BEAT multiply/shift; RELEASE0/1; others zero.
   STOP_ALL port/generation0. Acceptance is not resource pin or playback. */
int asql_command(asql_t *,const aica_cmd_t *,uint32_t active_session);
/* After MIDI and resource service, once per main pass. Urgent stop first, then
   <=32 events per bound port, then one ordinary control/resume step. Sample tick
   before a full4410Hz wrap; a too-large representable delta faults conservatively.
   Paused musical position/debt/fraction freeze; speed fades retain wall time. */
void asql_service(asql_t *,uint32_t tick);
#endif
