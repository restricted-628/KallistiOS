/* Original bounded producer control service; output remains caller-owned. */
#ifndef KOSDEC_ADX_SERVICE_H
#define KOSDEC_ADX_SERVICE_H
#include <kosdec/adx_pipe.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_command_kind {
    KOSDEC_ADX_PRODUCTION_PAUSE, /* enabled=false starts/resumes production. */
    KOSDEC_ADX_SEQUENCE_REPEAT,
    KOSDEC_ADX_RELEASE_LOOP,
    KOSDEC_ADX_ENQUEUE
};
struct kosdec_adx_command {
    enum kosdec_adx_command_kind kind;
    bool enabled; /* PAUSE/REPEAT only. */
    struct kosdec_adx_source source; /* ENQUEUE only; context borrowed. */
};
struct kosdec_adx_service {
    uint32_t magic, mailbox, cancel_requested, state;
    struct kosdec_adx_command command;
    enum kosdec_adx_status reply;
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
   Production pause/cancel supported; loop/repeat/enqueue return UNSUPPORTED.
   Feed and joint remain producer-only until all service users have joined. */
enum kosdec_adx_status kosdec_adx_service_init_joint(struct kosdec_adx_service *,
    struct kosdec_adx_joint_reader *, int16_t *pcm, size_t samples, size_t capacity);
/* One controller, serialized post/take calls. Exactly one outstanding command.
   MORE means copied/accepted, not applied: wake producer, then take its receipt.
   NEED_OUTPUT means previous receipt not collected; no mutation. Invalid command
   is BAD_ARGUMENT; closed service returns its terminal without accepting work.
   Sequence-only operations on a stream (or RELEASE_LOOP on a sequence) receive
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
