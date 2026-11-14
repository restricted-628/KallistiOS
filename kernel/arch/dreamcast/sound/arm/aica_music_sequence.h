/* Private sequence-to-MIDI adapter; one serialized ARM owner. */
#ifndef AICA_MUSIC_SEQUENCE_H
#define AICA_MUSIC_SEQUENCE_H
#include "aica_music.h"
#include "aica_sequence.h"
#include "aica_bank_registry.h"
#define AICA_TIMED_NOTES 64u
struct aica_music_sequences;
typedef struct {
    struct aica_music_sequences *owner;
    uint32_t generation,bank,bank_generation;
    uint8_t port,bound,resuming,cursor,sustain[16];
} aica_music_sequence_binding_t;
typedef struct {
    uint8_t live,port,channel,key,velocity,held,priority,reserved;
} aica_timed_note_t;
typedef struct aica_music_sequences {
    aica_music_t *music;
    aica_bank_registry_t *registry;
    uint32_t session,dropped;
    int (*retain_program)(void *,const aica_seq_program_t *);
    void (*release_program)(void *,const aica_seq_program_t *);
    void *context;
    aica_music_sequence_binding_t bindings[8];
    aica_sequence_t sequences[8];
    aica_timed_note_t notes[AICA_TIMED_NOTES];
} aica_music_sequences_t;
/* Before first use only. Program callbacks pin immutable arrays independently
   of bank pins. The adapter exclusively owns each bound music port; foreign
   direct MIDI/close/reset writers must not operate on it while bound. */
int aica_music_sequences_init(aica_music_sequences_t *,aica_music_t *,
    aica_bank_registry_t *,uint32_t session,
    int (*retain)(void *,const aica_seq_program_t *),
    void (*release)(void *,const aica_seq_program_t *),void *context);
/* Binding acquires a producer pin for the tone bank, including while paused.
   Unbind stops sequence work before dropping it; played voice pins remain. */
int aica_music_sequence_bind(aica_music_sequences_t *,unsigned port,uint32_t generation);
int aica_music_sequence_unbind(aica_music_sequences_t *,unsigned port);
/* Service an individual sequence with aica_sequence_service. Music service is
   separate and bounded; resume may need multiple music/sequence passes. */
#endif
