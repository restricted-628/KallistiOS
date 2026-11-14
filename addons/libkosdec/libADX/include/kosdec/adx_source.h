/* Original fixed-extent source selection and exclusive consumer loan. */
#ifndef KOSDEC_ADX_SOURCE_H
#define KOSDEC_ADX_SOURCE_H
#include <kosdec/adx_index.h>
#include <kosdec/adx_sequence.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_source_handle {
    uint32_t magic;
    struct kosdec_adx_source parent;
    struct kosdec_adx_view selected;
    struct kosdec_adx_index index;
    const uint8_t *memory;
    struct kosdec_adx_source *loan;
    bool selecting;
};
/* Initially/after close only. Immutable provider and disjoint storage survive
   this handle AND all consumers. All control/lookup/consumer calls serialized.
   No allocation, transport retirement or content decoding. Zero extent allowed.
   DONE on success; invalid args unchanged. Do not copy live handles. */
enum kosdec_adx_status kosdec_adx_source_init(struct kosdec_adx_source_handle *,
    kosdec_adx_read_at, void *context, uint32_t bytes);
enum kosdec_adx_status kosdec_adx_source_init_memory(struct kosdec_adx_source_handle *,
    const uint8_t *memory, uint32_t bytes);
/* Transactionally select a bounded range or ACX/AFS member in the ORIGINAL parent.
   Range selection is immediate; index_begin returns MORE, index_step performs
   at most one <=8-byte parent read. WAIT returns NEED_INPUT. Only DONE replaces
   the old selection; error/cancel retains it. No concurrent loan or selection.
   NEED_OUTPUT means an outstanding consumer loan; BAD_ARGUMENT invalid state.
   A source index selects compressed bytes, not PCM samples or an audible seek. */
enum kosdec_adx_status kosdec_adx_source_select_range(struct kosdec_adx_source_handle *,
    uint32_t offset, uint32_t bytes);
enum kosdec_adx_status kosdec_adx_source_index_begin(struct kosdec_adx_source_handle *,
    enum kosdec_adx_container, uint32_t ordinal);
enum kosdec_adx_status kosdec_adx_source_index_step(struct kosdec_adx_source_handle *);
enum kosdec_adx_status kosdec_adx_source_index_cancel(struct kosdec_adx_source_handle *);
/* Borrow one immutable descriptor into caller storage, which must initially
   have read_at==NULL. Usable by any existing stream/container/sequence/loader or
   sector-request API. Context/extent are selected and bounded, tag is caller's
   metadata. Descriptor and handle must not move or be mutated while loaned.
   Consumers may copy the descriptor; ALL such users must join before return.
   This is one serialized ownership loan, not one decoder or one read limit.
   The callback clamps reads to <=512 bytes and the selected extent. */
enum kosdec_adx_status kosdec_adx_source_borrow(struct kosdec_adx_source_handle *,
    struct kosdec_adx_source *out, uint32_t tag);
/* Return the exact descriptor storage after all consumers join; clears it.
   EOF/cancel/PCM drain is not a join. No provider cancellation or close. */
enum kosdec_adx_status kosdec_adx_source_return(struct kosdec_adx_source_handle *,
    struct kosdec_adx_source *loan);
/* Reject outstanding loan/selection with NEED_OUTPUT; otherwise invalidate.
   Parent provider and its independent transport retirement remain caller-owned. */
enum kosdec_adx_status kosdec_adx_source_close(struct kosdec_adx_source_handle *);
#ifdef __cplusplus
}
#endif
#endif
