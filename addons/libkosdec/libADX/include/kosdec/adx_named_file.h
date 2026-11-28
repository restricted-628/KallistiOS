/* Copyright (C) 2026 Kyle Mayer */
/* Original owned filename binding over the synchronous KOS VFS reader. */
#ifndef KOSDEC_ADX_NAMED_FILE_H
#define KOSDEC_ADX_NAMED_FILE_H
#include <kosdec/adx_stream.h>
#include <kosdec/adx_sequence.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_named_file;
/* All control calls are serialized on the creating ordinary KOS thread, with
   IRQs enabled, outside exceptions, TLS/switch hooks, shared fiber carriers and
   incompatible locks. No forced owner termination or injected wait errors.
   The private descriptor must never be extracted, duplicated, replaced or
   externally closed. No descriptor-table drain/global filesystem shutdown
   from construction through close. Retain a stable mount/media/source; do not
   unmount on this owner while the source is open.

   Only synchronous seekable immutable backends are supported: reads retain no
   caller buffer after return, and close retires backend state even on failure.
   Independent opens must have independent cursors. No hidden async providers.
   Control calls may block without a timeout; they do not cancel VFS waits.

   Raw paths of PATH_MAX bytes or more, excluding the terminator, fail with
   ENAMETOOLONG before allocation or VFS admission. VFS normalizes shorter
   paths and applies mount-specific limits.
   Open O_RDONLY, allocate private state, query extent (<=UINT32_MAX). Does not
   validate ADX content until decoding. Optional cache is NULL/0 or 512..32768
   caller-owned bytes, disjoint from live objects/buffers, retained through close.
   Backend alignment requirements apply. *out must initially be NULL; successful
   open publishes it. Argument/context rejection leaves both outputs unchanged.
   Once admitted, *cleanup_error=0 unless failed-construction close reports an
   error. Failure returns -1 with the PRIMARY errno and leaves *out NULL.
   No reopening by name on read errors, seek or loop. */
int kosdec_adx_named_file_open(struct kosdec_adx_named_file **out,
    const char *path, bool repeat, uint8_t *cache, size_t cache_bytes,
    int *cleanup_error);
/* One exclusive loan of the initialized decoder stream; *stream initially NULL.
   Caller may transfer this loan to a serialized decoder/player producer. Do not
   reinitialize/copy the stream or edit/extract its provider/context. No concurrent
   cursor users. A second borrow fails EBUSY. Outputs unchanged on rejection. */
int kosdec_adx_named_file_borrow(struct kosdec_adx_named_file *,
    struct kosdec_adx_stream **stream);
/* Return the exact loan after ALL readers/player producers have joined. EOF,
   cancellation, command acknowledgement and PCM drain alone are not joins.
   This call records the caller's completed join; it does not perform one.
   Invalid/wrong-owner returns leave the loan intact. Success clears *stream.
   The returned stream's state is preserved for a subsequent borrow. */
int kosdec_adx_named_file_return(struct kosdec_adx_named_file *,
    struct kosdec_adx_stream **stream);
/* Alternative exclusive loan of immutable file bytes for source selection,
   ACX/AFS lookup, PCM containers, sequence/loader or sector requests. out must
   have read_at==NULL and retain its address through return. The private fd is
   never exposed. Do not inspect the opaque callback context or modify the
   descriptor. Copies may be consumed serially, but ALL users must join before
   returning the exact original descriptor. Mutually exclusive with stream loan.
   File/cache/creating-thread and synchronous backend contracts above apply.
   This operation does not decode or reset the existing ADX stream. */
int kosdec_adx_named_file_borrow_source(struct kosdec_adx_named_file *,
    struct kosdec_adx_source *out, uint32_t tag);
int kosdec_adx_named_file_return_source(struct kosdec_adx_named_file *,
    struct kosdec_adx_source *loan);
/* Reject a live loan with EBUSY before side effects. Other precondition failures
   also retain ownership and outputs. An admitted close consumes the source
   exactly once, clears *file, returns 0 and sets *cleanup_error (0 on success).
   A nonzero cleanup_error does NOT permit retry: the fd may already be reused.
   Passing an already-cleared handle fails EINVAL without another VFS close.
   These consumption rules rely on the restricted contract above and do not
   change generic fs_close behavior. No audible/device retirement is implied. */
int kosdec_adx_named_file_close(struct kosdec_adx_named_file **file,
    int *cleanup_error);
#ifdef __cplusplus
}
#endif
#endif
