/* Original bounded producer control service; output remains caller-owned. */
#ifndef KOSDEC_ADX_SERVICE_H
#define KOSDEC_ADX_SERVICE_H
#include <kosdec/adx_pipe.h>
#include <kosdec/adx_loader.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_command_kind {
    KOSDEC_ADX_PRODUCTION_PAUSE, /* enabled=false starts/resumes production. */
    KOSDEC_ADX_SEQUENCE_REPEAT,
    KOSDEC_ADX_RELEASE_LOOP,
    KOSDEC_ADX_ENQUEUE,
    KOSDEC_ADX_RELEASE_LINK, /* Finish current adjacent ADX record. */
    KOSDEC_ADX_LOADER_ENQUEUE, /* Register a resolved file/AFS member. */
    KOSDEC_ADX_LOADER_FINISH, /* End registration and seal at transport EOF. */
    KOSDEC_ADX_SET_LINK_FOLLOW /* Change the active linked-source switch. */
};
struct kosdec_adx_command {
    enum kosdec_adx_command_kind kind;
    bool enabled; /* PAUSE/REPEAT/SET_LINK_FOLLOW only. */
    struct kosdec_adx_source source; /* ENQUEUE or LOADER_ENQUEUE; context borrowed. */
    char label[KOSDEC_ADX_LOADER_NAME_BYTES]; /* LOADER_ENQUEUE only; NUL-terminated. */
};
struct kosdec_adx_command_receipt {
    enum kosdec_adx_status status;
    uint64_t source_id; /* Nonzero only after a successful LOADER_ENQUEUE. */
};
struct kosdec_adx_service {
    uint32_t queued_sources, link_enabled; /* Atomic producer publication; not directly writable. */
    uint32_t input_data, input_capacity; /* Atomic input snapshot and immutable bind capacity. */
    uint32_t magic, mailbox, cancel_requested, state;
    struct kosdec_adx_command command;
    enum kosdec_adx_status reply;
    uint64_t reply_source_id;
    struct kosdec_adx_pipe pipe;
};
struct kosdec_adx_service_state {
    enum kosdec_adx_status status; /* MORE or sticky producer terminal. */
    bool paused; /* Production only, not audible playback. */
};
/* Initialize while all users are stopped. Same source/storage requirements as
   pipe_init; source must be unpaused, sequence already started. Begins with
   production paused. Invalid init leaves state unchanged. No allocation.
   While bound, service_step exclusively owns source/pipe producer operations;
   consumer uses service.pipe's read/get_format. Never pump/cancel the pipe or
   mutate its source directly. All fields are implementation-owned. */
enum kosdec_adx_status kosdec_adx_service_init(struct kosdec_adx_service *,
    struct kosdec_adx_stream *, struct kosdec_adx_sequence *,
    int16_t *pcm, size_t samples, size_t capacity);
/* Joint alternative: same initial pause and exclusive producer ownership.
   Feed, linked loader and joint remain producer-only until all service users
   have joined. Configure a live loader with seal_when_empty=false. */
enum kosdec_adx_status kosdec_adx_service_init_joint(struct kosdec_adx_service *,
    struct kosdec_adx_joint_reader *, int16_t *pcm, size_t samples, size_t capacity);
/* WAV source; same producer mailbox and initial-paused policy. Sequence and
   embedded ADX loop commands are unsupported; seek/reinit requires unbinding. */
enum kosdec_adx_status kosdec_adx_service_init_wave(struct kosdec_adx_service *,
    struct kosdec_adx_wave *, int16_t *pcm, size_t samples, size_t capacity);

/* AU PCM/mu-law source; same producer mailbox and initial-paused policy. Sequence and
   embedded ADX loop commands are unsupported; seek/reinit requires unbinding. */
enum kosdec_adx_status kosdec_adx_service_init_au(struct kosdec_adx_service *,
    struct kosdec_adx_au *, int16_t *pcm, size_t samples, size_t capacity);

/* AIFF PCM source; same producer mailbox and initial-paused policy. Sequence and
   embedded ADX loop commands are unsupported; seek/reinit requires unbinding. */
enum kosdec_adx_status kosdec_adx_service_init_aiff(struct kosdec_adx_service *,
    struct kosdec_adx_aiff *, int16_t *pcm, size_t samples, size_t capacity);
/* SPSD mono/stereo PCM16; same source lifetime and output rules. */
enum kosdec_adx_status kosdec_adx_service_init_spsd(struct kosdec_adx_service *,
    struct kosdec_adx_spsd *, int16_t *pcm, size_t samples, size_t capacity);

/* Any thread: pending sequence decode entries or pending linked-loader reads.
   Each count includes the active entry at its own stage.
   Updated at initialization and after producer commands/steps. Zero for other
   source kinds. Independent snapshot, not a transaction with PCM or mailbox;
   a collected ENQUEUE receipt includes its count publication. Cancel retains
   source count until users join; it is not an audible pending-file count.
   False for invalid arguments leaves output unchanged. */
bool kosdec_adx_service_get_queue_count(const struct kosdec_adx_service *, uint32_t *);

/* Any thread: last producer-published follow setting for a stream or linked
   joint. False for invalid/unsupported services leaves output unchanged. Remains the requested
   setting at EOF; release publishes false before its receipt. Not an audible
   transition indicator or a coherent snapshot with queue/PCM counters. */
bool kosdec_adx_service_get_linked(const struct kosdec_adx_service *, bool *enabled);

/* One controller, serialized post/take calls. Exactly one outstanding command.
   MORE means copied/accepted, not applied: wake producer, then take its receipt.
   NEED_OUTPUT means previous receipt not collected; no mutation. Invalid command
   is BAD_ARGUMENT; closed service returns its terminal without accepting work.
   REPEAT controls a sequence or a linked loader. ENQUEUE needs a sequence.
   LOADER_ENQUEUE copies one complete already-resolved file or AFS view. The
   label is metadata. It may revive a live loader waiting on an empty queue.
   LOADER_FINISH closes registration, disables repeat, and seals after pending
   reads complete. Further loader enqueues and repeat enable are rejected.
   Unsupported source/command combinations receive
   UNSUPPORTED. Enqueue may receive NEED_OUTPUT for a full16-entry sequence.
   Keep borrowed source context alive until receipt, and if enqueued, throughout
   all queued/active/repeated uses. Payload storage itself may be reused on return.
   An accepted command racing terminal closure always has a collectable receipt,
   even if the producer exits. A terminal receipt means it was not applied. */
enum kosdec_adx_status kosdec_adx_service_post(struct kosdec_adx_service *,
    const struct kosdec_adx_command *);
/* Controller-only; false leaves result unchanged. Receipt is collected once.
   MORE means control applied, other values describe rejection. No waiting. */
bool kosdec_adx_service_take(struct kosdec_adx_service *, enum kosdec_adx_status *);
/* Alternative to take for the same one-shot receipt, with the assigned loader
   ID. A rejected command retains no source loan in the producer. Successful
   admission requires the original provider to survive through producer join
   and separate backend retirement. */
bool kosdec_adx_service_take_receipt(struct kosdec_adx_service *,
                                    struct kosdec_adx_command_receipt *);
/* Producer-only. Process one command OR one pipe pump. Commands do no I/O and
   return MORE/0; obtain their result through take. Paused steps return MORE/0.
   Cancellation takes priority at entry; an in-progress step can still finish.
   Source callbacks may block: use the nonblocking disc provider on a shared
   fiber executor. Caller schedules/yields/waits and wakes after post/cancel or
   consumer progress. EOF/error/cancel closes command admission permanently. */
struct kosdec_adx_stream_result kosdec_adx_service_step(struct kosdec_adx_service *);
/* Any thread. Wake the producer after requesting cancel. Pending controls are
   rejected on closure, published PCM remains drainable, existing terminals win.
   Neither receipt, terminal nor empty pipe proves producer return, joined users,
   I/O retirement or final audio fetch. Stop callback wake sources and join all
   users, then separately retire providers/downstream users before free/reinit. */
void kosdec_adx_service_request_cancel(struct kosdec_adx_service *);
struct kosdec_adx_service_state kosdec_adx_service_get_state(
    const struct kosdec_adx_service *);
#ifdef __cplusplus
}
#endif
#endif
