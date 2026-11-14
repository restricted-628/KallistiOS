/* Original private SH4 owner for the experimental resident bank registry. */
#ifndef ABK_REGISTRY_H
#define ABK_REGISTRY_H
#include "abk_upload.h"
#include <dc/sound/aica_bank_registry.h>
typedef enum {
    ABKB_EMPTY=0, ABKB_STAGING, ABKB_STAGED, ABKB_PUBLISHING, ABKB_ACTIVE,
    ABKB_UNREGISTERING, ABKB_RETIRED, ABKB_FAILED, ABKB_LOST, ABKB_CLOSED
} abk_bank_state_t;
/* Initialize with {0}. Caller keeps object alive until CLOSED; all mutations
   go through this module's common serialization, including staging close/cancel.
   Never call upload APIs on staging directly. No concurrent sound reset/shutdown,
   raw address users or another registry manager. Source/arrays immutable until
   staging moves to ACTIVE or the bank closes. All observations are read-only. */
typedef struct {
    abk_bank_state_t state;
    abk_upload_t staging;
    uint32_t slot, session, generation, cookie, address, bytes, pending;
    int closing, first_error, last_error;
} abk_bank_t;
int abk_bank_stage(abk_bank_t *b, unsigned slot, const void *data, uint32_t size,
                   abk_sample_t *samples, uint32_t sample_capacity,
                   abk_zone_t *zones, uint32_t zone_capacity,
                   uint32_t chunk_limit, uint32_t lifetime_ms);
/* Pump bounded upload/publication/unregistration progress. No implicit publish.
   Deadlines bound individual command submission/bind; pending ACKs retain memory.
   Poll again after EAGAIN/EBUSY; first error and latest error stay separate. */
int abk_bank_poll(abk_bank_t *b, uint32_t timeout_ms);
int abk_bank_publish(abk_bank_t *b, uint32_t timeout_ms);
/* Latches admission closed. Cancels upload or retires publication before freeing.
   Continue polling until CLOSED. LOST requires external qualified recovery and
   never frees here. No voice unpin is implied by this operation. */
int abk_bank_close(abk_bank_t *b, uint32_t timeout_ms);
/* Future bank-aware commands reserve before enqueue and release only after
   known admission failure or exact terminal ACK, after ARM pins are established.
   These functions do not manufacture that receipt/physical retirement proof. */
int abk_bank_pending_acquire(abk_bank_t *b);
int abk_bank_pending_release(abk_bank_t *b);
/* Irreversibly stop local bank admission; EBUSY until every bank is CLOSED.
   This is not global sound shutdown and does not disable/reset any device. */
int abk_bank_quiesce(void);
#endif
