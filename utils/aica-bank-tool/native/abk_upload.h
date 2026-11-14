/* Original private SH-4 upload transaction. No ARM/public bank ABI. */
#ifndef ABK_UPLOAD_H
#define ABK_UPLOAD_H
#include "abk.h"
#include <dc/spu.h>

typedef enum {
    ABKU_EMPTY = 0, ABKU_UPLOADING, ABKU_RETIRING, ABKU_READY,
    ABKU_FAILED, ABKU_CANCELLED, ABKU_CLOSED, ABKU_IMAGE_READY, ABKU_MOVED
} abk_upload_state_t;

/* Initialize with {0}. Read fields to observe; never modify while live.
   One serialized ordinary-thread owner. The sound runtime/allocator must stay
   live, with no reset/shutdown. No channel/ARM consumer may use this allocation.
   Keep this object, input and descriptor arrays alive/disjoint until close.
   READY means copied, unpublished PCM only. It is not playable-bank acceptance. */
typedef struct {
    abk_upload_state_t state;
    int first_error, last_error;
    abk_result_t validation;
    const void *data;
    abk_plan_t plan;
    abk_placement_t placement;
    spu_transfer_request_t *request;
    uint32_t address, committed, chunk_bytes, chunk_limit, lifetime_ms;
    int cancelled, image;
} abk_upload_t;

/* No service calls on invalid input. Descriptor capacities <= format maxima.
   Chunk limit32..32768, multiple32. Nonzero lifetime includes queue residence
   for EACH request; it is not a whole-bank or physical-retirement deadline.
   Validated descriptors may be written even if subsequent allocation fails.
   Any admitted transaction, including failure, must eventually be closed. */
int abk_upload_begin(abk_upload_t *t, const void *data, uint32_t size,
                     abk_sample_t *samples, uint32_t sample_capacity,
                     abk_zone_t *zones, uint32_t zone_capacity,
                     uint32_t chunk_limit, uint32_t lifetime_ms);

/* Explicit resident-image path: stages the complete immutable original ABK,
   including metadata. Allocation must fit actual RAM. IMAGE_READY is distinct
   from PCM READY. Only the serialized bank owner may move ownership afterward. */
int abk_upload_begin_image(abk_upload_t *t, const void *data, uint32_t size,
                           abk_sample_t *samples, uint32_t sample_capacity,
                           abk_zone_t *zones, uint32_t zone_capacity,
                           uint32_t chunk_limit, uint32_t lifetime_ms);

/* At most one status observation, then retire or submit at most one request.
   Returns0 on progress/settled success; -1 sets errno and last_error. EBUSY from
   destruction is a retry, not a failed upload. Other service failures latch
   first_error and permanently stop admission. No loop or wait is hidden here. */
int abk_upload_pump(abk_upload_t *t);

/* Latched cancellation prevents READY even when completion wins the race.
   Call pump until the request retires, then close. Repeated cancel is safe. */
int abk_upload_cancel(abk_upload_t *t);

/* Refuses a live request, without implicit cancellation. With no consumers,
   release is permitted after request destruction. snd_mem_free is void: errno
   is checked under the live/exclusive/no-reset contract, not a new allocator
   guarantee. Observed failure retains address/ownership for explicit retry.
   Repeated close and pump/cancel after CLOSED make no native service calls. */
int abk_upload_close(abk_upload_t *t);
#endif
