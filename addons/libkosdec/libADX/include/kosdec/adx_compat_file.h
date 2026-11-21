/* Caller-owned FILE observations of bounded STREAM transfers. */
#ifndef KOSDEC_ADX_COMPAT_FILE_H
#define KOSDEC_ADX_COMPAT_FILE_H
#include <kosdec/adx_request.h>
#include <kosdec/adx_sequence.h>
#ifdef __cplusplus
extern "C" {
#endif

#define KOSDEC_ADX_COMPAT_MAX_FILES 16u

struct kosdec_adx_compat_file_info {
    uint32_t cursor, start, requested, completed;
    uint32_t transferred; /* Full sectors accepted by STREAM in this request. */
    enum kosdec_adx_request_status state, transfer;
};

typedef bool (*kosdec_adx_compat_retry_error)(void *context, uint32_t byte_offset);

struct kosdec_adx_compat_file {
    struct kosdec_adx_request request;
    kosdec_adx_read_at read_at;
    void *context;
    kosdec_adx_compat_retry_error retry_error;
    struct kosdec_adx_compat_file_info info;
    struct kosdec_adx_joint *joint;
    struct kosdec_adx_chunk held;
    uint8_t *output;
    uint32_t next_limit, active_limit;
    unsigned read_errors;
    bool retryable;
};

struct kosdec_adx_compat_files {
    struct kosdec_adx_compat_file *slots;
    unsigned capacity;
};

/* Initialize disjoint caller storage, initially or after closing every slot.
   Bind observe to FILE and stream to STREAM via kosdec_adx_compat_config.
   All calls and callbacks are serialized by the same ordinary owner.
   Slots are implementation-owned until closed. A provider must not reenter.
   0 on success, -1 with errno for invalid arguments or ownership failures. */
int kosdec_adx_compat_files_init(struct kosdec_adx_compat_files *,
    struct kosdec_adx_compat_file *, unsigned capacity);
/* Open an empty slot over a borrowed finite read_at descriptor. The provider
   stays alive through close and any independent provider/DMA retirement. */
int kosdec_adx_compat_file_open(struct kosdec_adx_compat_file *,
    const struct kosdec_adx_source *);
/* Capture a positive maximum batch for the NEXT logical read. Default 512. */
int kosdec_adx_compat_file_set_batch(struct kosdec_adx_compat_file *, uint32_t sectors);
/* Optional classifier for empty provider IO_ERROR replies, using the provider
   context. Set outside an active request. A retry rewinds the uncommitted
   batch. The 12th consecutive retryable failure ends the transfer. A successful
   batch resets the error run. NULL restores immediate definitive errors. */
int kosdec_adx_compat_file_set_retry(struct kosdec_adx_compat_file *,
    kosdec_adx_compat_retry_error);
/* Admit an EOF-clipped read at the committed cursor; an admitted zero-sector
   read is finalized by FILE. While READING, a second start returns EBUSY.
   Keep the entire output range reserved until FILE completes or stop succeeds.
   The final short physical sector is zero-padded. No provider call at admission. */
int kosdec_adx_compat_file_start(struct kosdec_adx_compat_file *, uint32_t sectors,
    uint8_t *output, size_t capacity);
/* Publish whole sector batches to a RING or QUEUE. The final short sector
   includes zero padding. A batch fits wholly in one contiguous FREE span.
   Split SEPARATE queue nodes require an extra workspace node. Keep the joint
   and its storage live and allow only this FREE producer until FILE completes
   or stop succeeds. The caller can consume DATA between visits. */
int kosdec_adx_compat_file_start_joint(struct kosdec_adx_compat_file *,
    uint32_t sectors, struct kosdec_adx_joint *);
/* Bind these callbacks to the same owner's FILE and STREAM phases. FILE
   snapshots full transferred sectors and commits the cursor on a terminal
   transfer. STREAM alone calls the provider, up to four steps of <=512 bytes
   per slot per visit, or just admits a batch. WAIT yields to the next visit.
   A definitive provider failure is reported via the next FILE snapshot.
   The first internal release failure is returned, after visiting all slots. */
int kosdec_adx_compat_files_observe(void *);
int kosdec_adx_compat_files_stream(void *);
/* Stop preserves the committed cursor, samples the transferred count and
   releases unfinished output. Previously published DATA stays in the joint.
   No provider call or backend cancellation. A failed release retains ownership. */
int kosdec_adx_compat_file_stop(struct kosdec_adx_compat_file *);
int kosdec_adx_compat_file_seek(struct kosdec_adx_compat_file *,
    int64_t offset, enum kosdec_adx_request_origin);
int kosdec_adx_compat_file_get_info(const struct kosdec_adx_compat_file *,
    struct kosdec_adx_compat_file_info *);
int kosdec_adx_compat_file_close(struct kosdec_adx_compat_file *);

#ifdef __cplusplus
}
#endif
#endif
