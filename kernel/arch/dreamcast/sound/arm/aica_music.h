/* Original private resident music engine. No SDK code or wire compatibility. */
#ifndef AICA_MUSIC_H
#define AICA_MUSIC_H
#include <stdint.h>
#include <dc/sound/aica_comm.h>
#define AICA_MUSIC_PORTS 8u
#define AICA_MUSIC_CHANNELS 16u
#define AICA_MUSIC_VOICES 64u
#define AICA_MUSIC_QUEUE 64u
#define AICA_MUSIC_EVENTS_PER_PASS 4u
#define AICA_MUSIC_VOICE_BUDGET 16u
#define AICA_MUSIC_OK 0
#define AICA_MUSIC_INVALID -1
#define AICA_MUSIC_BUSY -2
#define AICA_MUSIC_MISSING -3
#define AICA_MUSIC_EXHAUSTED -4
#define AICA_MUSIC_RETRY -5
#define AICA_MUSIC_FAULT -6
#define AICA_MUSIC_IDLE 0u
#define AICA_MUSIC_HELD 1u
#define AICA_MUSIC_SUSTAINED 2u
#define AICA_MUSIC_RELEASE 3u
#define AICA_MUSIC_VOLUME 0u
#define AICA_MUSIC_PAN 1u
#define AICA_MUSIC_PITCH 2u
#define AICA_MUSIC_DIRECT 3u
#define AICA_MUSIC_EFFECT 4u

typedef struct {
    aica_channel_config_t config;
    uint32_t bank, generation;
    int16_t fine_cents;
    uint8_t root_key, reserved;
} aica_music_tone_t;
typedef struct {
    uint32_t generation, bank, bank_generation;
    uint8_t port, channel, status, a, b, priority, reserved[2];
} aica_music_event_t;
typedef struct {
    int32_t from, value, target;
    uint32_t elapsed, duration;
} aica_music_fade_t;
typedef struct {
    uint8_t program, bank, volume, expression, pan, sustain, modulation;
    uint8_t bend_semitones, bend_cents, rpn_msb, rpn_lsb, reserved_byte;
    int16_t bend;
    uint16_t reserved;
} aica_music_channel_t;
typedef struct {
    aica_music_channel_t channels[AICA_MUSIC_CHANNELS];
    aica_music_fade_t fades[3];
    uint32_t generation, bank, bank_generation;
    uint16_t mute;
    int16_t direct, effect;
    uint8_t open, gm, release_mode, mailbox;
} aica_music_port_t;
typedef struct {
    aica_music_tone_t tone;
    uint32_t age, token, port_generation;
    uint8_t state, port, channel, key, velocity, priority, dirty, reserved;
} aica_music_voice_t;
typedef struct {
    /* One call may do bounded incremental lookup and return RETRY. The event
       stays at the queue head until it finishes or is invalidated. Immediate
       controls may change selectors; the resolver must restart that lookup.
       No returned pointer may outlive an immutable registered bank. */
    int (*resolve)(void *,const aica_music_event_t *,unsigned bank,unsigned program,
                   unsigned drum,aica_music_tone_t *);
    /* Establish an idempotent retained channel/bank lease before any hardware
       START. Logical retirement never releases that physical-lifetime lease. */
    void (*cancel_resolve)(void *);
    int (*retain)(void *,unsigned,const aica_music_tone_t *);
    int (*start)(void *,unsigned,uint32_t,const aica_channel_config_t *);
    void (*update)(void *,unsigned,const aica_channel_config_t *);
    void (*stop)(void *,unsigned,int release);
    /* Qualified same-start logical completion only; does not release banks. */
    int (*ended)(void *,unsigned,uint32_t);
    void *context;
} aica_music_ops_t;
typedef struct {
    aica_music_port_t ports[AICA_MUSIC_PORTS];
    aica_music_voice_t voices[AICA_MUSIC_VOICES];
    aica_music_event_t queue[AICA_MUSIC_QUEUE];
    aica_music_ops_t ops;
    uint32_t token, tick, admitted, applied, declined, rejected, stolen;
    int32_t last_result;
    uint16_t head, count;
    uint8_t voice_count, voice_cursor, quiescing, faulted;
    uint8_t fault_voice, reserved[3];
} aica_music_t;
/* All operations are serialized by the single ARM owner. No allocation/MMIO
   outside the supplied callbacks. Pool entries are already exclusively owned.
   Reinitialization is only legal before any consumer can reference the engine. */
int aica_music_init(aica_music_t *,unsigned voices,const aica_music_ops_t *);
int aica_music_open(aica_music_t *,unsigned port,uint32_t bank,uint32_t generation,
                    uint32_t *port_generation);
/* Close invalidates queued work and stops logical voices; retained bank/channel
   leases remain owned by the enclosing runtime. Never treat close as free. */
int aica_music_close(aica_music_t *,unsigned port,uint32_t generation);
int aica_music_message(aica_music_t *,unsigned port,uint32_t generation,
                       unsigned status,unsigned a,unsigned b,unsigned priority);
/* Urgent logical stop for a port owner. Cancel queued note events, retain
   queued controller/program changes and all physical bank leases. */
int aica_music_silence(aica_music_t *,unsigned port,uint32_t generation,unsigned release);
int aica_music_mute(aica_music_t *,unsigned port,uint32_t generation,unsigned mask);
int aica_music_gm(aica_music_t *,unsigned port,uint32_t generation,unsigned enabled);
int aica_music_release_mode(aica_music_t *,unsigned port,uint32_t generation,unsigned enabled);
int aica_music_parameter(aica_music_t *,unsigned port,uint32_t generation,
                         unsigned parameter,int value,uint32_t fade_ms);
int aica_music_reset(aica_music_t *,unsigned port,uint32_t generation);
/* Up to4 events and16 periodic hardware visits; controls scan at most64 voices. Resolver must itself bound each lookup step.
   elapsed_ms is real elapsed time, not a request to replay every missed tick. */
unsigned aica_music_service(aica_music_t *,uint32_t elapsed_ms);
void aica_music_quiesce(aica_music_t *);
/* Original equal-tempered pitch conversion, 256 units/semitone; clamps to the
   existing native channel frequency domain1..192000. No floating-point runtime. */
uint32_t aica_music_rate(uint32_t rate,int32_t semitone256);
#endif
