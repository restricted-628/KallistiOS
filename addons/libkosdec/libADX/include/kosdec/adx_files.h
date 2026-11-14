/* Original explicit filename handle collection; no process-global file state. */
#ifndef KOSDEC_ADX_FILES_H
#define KOSDEC_ADX_FILES_H
#include <kosdec/adx_named_file.h>
#ifdef __cplusplus
extern "C" {
#endif
#define KOSDEC_ADX_FILES_MAX 32u
#define KOSDEC_ADX_FILENAME_BYTES 256u
struct kosdec_adx_files_entry {
    uint64_t id;
    struct kosdec_adx_named_file *file;
    char name[KOSDEC_ADX_FILENAME_BYTES];
};
struct kosdec_adx_files {
    uint32_t magic;
    uint64_t next_id;
    unsigned capacity;
    struct kosdec_adx_files_entry *entries;
};
struct kosdec_adx_files_result {
    uint32_t closed, retained, cleanup_failed; /* Slot masks, not handle IDs. */
    int first_error; /* First rejection or consumed-close cleanup error. */
};
/* Caller-owned collection/workspace, 1..32 slots, initially/after finish only.
   All operations use the named-file creating ordinary-thread contract. Workspace
   is disjoint and stable until successful finish; never copy/reinit live state.
   0 success, -1 errno. IDs never repeat within a collection lifetime; a full or
   exhausted collection returns ENOSPC/EOVERFLOW before opening. No global reset. */
int kosdec_adx_files_init(struct kosdec_adx_files *,
    struct kosdec_adx_files_entry *workspace, unsigned capacity);
/* Copy nonempty path<=255 bytes. Native KOS path; no process directory changes.
   Cached input storage has named-file lifetime and eligibility requirements.
   *id unchanged on failure; cleanup_error follows named_file_open. */
int kosdec_adx_files_open(struct kosdec_adx_files *, const char *path,
    uint8_t *cache, size_t cache_bytes, uint64_t *id, int *cleanup_error);
/* Source descriptors enable the common bounded source/index/container/loader
   and request APIs. One exclusive loan per file. Return after ALL consumers join.
   No filename reopening or implicit provider cancellation. */
int kosdec_adx_files_borrow(struct kosdec_adx_files *, uint64_t id,
    struct kosdec_adx_source *out, uint32_t tag);
int kosdec_adx_files_return(struct kosdec_adx_files *, uint64_t id,
    struct kosdec_adx_source *loan);
/* Copy original name including NUL; ERANGE leaves output unchanged. */
int kosdec_adx_files_get_name(const struct kosdec_adx_files *, uint64_t id,
    char *out, size_t capacity);
/* Admitted close returns0 and invalidates ID even when cleanup_error!=0.
   Rejection retains ID/resources and cleanup output unchanged; never retry a
   consumed close. A live source loan returns EBUSY through the named source. */
int kosdec_adx_files_close(struct kosdec_adx_files *, uint64_t id, int *cleanup_error);
/* Try every occupied slot, continuing after independent failures. Result masks
   identify consumed/rejected/cleanup-error slots. Returns -1 if ANY rejection
   or cleanup error occurred, errno=first_error. Consumed IDs stay invalid; retry
   only retained IDs. No implicit loan return, decoder join or sound operation. */
int kosdec_adx_files_close_all(struct kosdec_adx_files *, struct kosdec_adx_files_result *);
/* Empty collection only; EBUSY unchanged otherwise. Then workspace reusable. */
int kosdec_adx_files_finish(struct kosdec_adx_files *);
#ifdef __cplusplus
}
#endif
#endif
