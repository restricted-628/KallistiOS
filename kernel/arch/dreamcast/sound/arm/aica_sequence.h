/* Original private native sequence core. Not an SDK data format or ABI. */
#ifndef AICA_SEQUENCE_H
#define AICA_SEQUENCE_H
#include <stdint.h>
#define AICA_SEQ_BUDGET 32u
#define AICA_SEQ_DEPTH 4u
#define AICA_SEQ_MIDI 1u
#define AICA_SEQ_TEMPO 2u
#define AICA_SEQ_LOOP 3u
#define AICA_SEQ_LOOP_END 4u
#define AICA_SEQ_END 5u
#define AICA_SEQ_IDLE 0u
#define AICA_SEQ_RUNNING 1u
#define AICA_SEQ_PAUSED 2u
#define AICA_SEQ_FAULT 3u
typedef struct {
    uint32_t delta, value, auxiliary;
    uint8_t kind, status, a, b;
} aica_seq_event_t;
typedef struct {
    const aica_seq_event_t *events;
    uint32_t count, ticks_per_quarter, tempo_us;
} aica_seq_program_t;
typedef struct {
    aica_seq_program_t program;
    uint64_t ticks, loop_ticks[AICA_SEQ_DEPTH];
    uint32_t index, begin[AICA_SEQ_DEPTH], end[AICA_SEQ_DEPTH];
    uint8_t depth, ready, failed, reserved;
} aica_seq_validation_t;
typedef struct {
    /* Programs are caller-owned and immutable throughout validation and every
       retained use. Pins include the complete event array, not just descriptor. */
    int (*retain)(void *,const aica_seq_program_t *);
    void (*release)(void *,const aica_seq_program_t *);
    /* emit: 1 accepted, 0 backpressure, negative permanent failure. No pointer
       into the sequence is retained by emit. pause/stop only retire logical
       sequence work; physical sample references belong to the MIDI backend. */
    int (*emit)(void *,unsigned status,unsigned a,unsigned b,unsigned priority);
    void (*silence)(void *,int release,int pause);
    /* Restore paused timed notes: same result convention as emit. */
    int (*resume)(void *);
    void *context;
} aica_seq_ops_t;
typedef struct {
    aica_seq_program_t program, chained;
    aica_seq_ops_t ops;
    uint64_t pending, remaining, ticks;
    uint32_t cursor, tempo, speed_remainder, processed;
    uint32_t loop_cursor[AICA_SEQ_DEPTH], loop_remaining[AICA_SEQ_DEPTH];
    uint32_t speed_from, speed_target, speed_elapsed, speed_duration;
    uint8_t state, priority, depth, loaded, counted, chain, exit_loop, release_mode;
    uint8_t beat_shift, beat_multiply, reserved[2];
} aica_sequence_t;
/* Caller keeps a validation pin until play/chain has independently retained it.
   Ready validation is invalidated by ANY program/array modification. */
int aica_seq_validate_begin(aica_seq_validation_t *,const aica_seq_program_t *);
/* 1 ready, 0 pending, -1 malformed; visits <=32 events. */
int aica_seq_validate_step(aica_seq_validation_t *);
int aica_sequence_init(aica_sequence_t *,const aica_seq_ops_t *);
/* 1 started, 0 lower-priority decline, -1 invalid, -2 pin failure. */
int aica_sequence_play(aica_sequence_t *,const aica_seq_validation_t *,unsigned priority);
void aica_sequence_stop(aica_sequence_t *);
int aica_sequence_pause(aica_sequence_t *);
int aica_sequence_continue(aica_sequence_t *);
/* NULL means exit active loops without chaining. Replacing a pending chain
   acquires the new reference before releasing the previous reference. */
int aica_sequence_chain(aica_sequence_t *,const aica_seq_validation_t *);
int aica_sequence_speed(aica_sequence_t *,int relative,uint32_t fade_ms);
/* A beat is whole_note *2^shift or whole_note /2^shift: multiply shift0..3,
   divide shift0..6. Default quarter note. */
int aica_sequence_beat(aica_sequence_t *,unsigned multiply,unsigned shift);
uint64_t aica_sequence_ticks(const aica_sequence_t *);
uint64_t aica_sequence_beats(const aica_sequence_t *);
/* Real elapsed microseconds. Debt is preserved across bounded event passes.
   Stable array order breaks equal-time ties. No MMIO or timer unit assumption. */
unsigned aica_sequence_service(aica_sequence_t *,uint32_t elapsed_us);
#endif
