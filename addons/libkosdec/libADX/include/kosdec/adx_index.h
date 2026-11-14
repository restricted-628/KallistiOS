/* Original indexed container lookup and bounded member input views. */
#ifndef KOSDEC_ADX_INDEX_H
#define KOSDEC_ADX_INDEX_H
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
enum kosdec_adx_container { KOSDEC_ADX_ACX, KOSDEC_ADX_AFS };
struct kosdec_adx_view {
    uint32_t magic, offset, bytes;
    kosdec_adx_read_at read_at;
    void *context;
};
struct kosdec_adx_index {
    uint32_t magic, input_bytes, ordinal, count, table_end, entry_offset;
    kosdec_adx_read_at read_at;
    void *context;
    enum kosdec_adx_container format;
    enum kosdec_adx_status terminal;
    bool header_ready;
    unsigned used;
    uint8_t scratch[8];
    struct kosdec_adx_view member;
};
/* Construct a bounded view of an already validated/selected source range.
   DONE copies the range; BAD_ARGUMENT leaves output unchanged. No I/O.
   offset<=input_bytes and bytes<=input_bytes-offset, including empty EOF views.
   Source callback/context and immutable bytes must outlive all view consumers;
   serialization follows read_at. This does not validate a container or payload. */
enum kosdec_adx_status kosdec_adx_view_init(struct kosdec_adx_view *,
    kosdec_adx_read_at, void *context, uint32_t input_bytes,
    uint32_t offset, uint32_t bytes);
/* Caller-owned serialized state; immutable source/context lifetime must extend
   through lookup AND every resulting member consumer. Reuses stream read_at's
   synchronous destination/no-reentry contract. No allocation, open, close or
   scheduler work. Explicit formats: ACX zero first word/BE32 fields, AFS NUL
   signature/LE32 fields. No detection of other versions or chained partitions.
   Fields are inspectable but implementation-owned. */
void kosdec_adx_index_init(struct kosdec_adx_index *, kosdec_adx_read_at,
    void *context, uint32_t input_bytes, enum kosdec_adx_container, uint32_t ordinal);
void kosdec_adx_index_cancel(struct kosdec_adx_index *);
/* One source call<=8 bytes per step; positive partial reads return MORE and WAIT
   returns NEED_INPUT. DONE publishes member only after selected range validation.
   NOT_FOUND means ordinal>=count; empty selected entries are UNSUPPORTED.
   Validates header/table extent and selected member bounds, not other entries,
   payload format, overlapping members or optional filename/timestamp metadata.
   count valid after header_ready, member valid only on DONE. Terminal results
   are sticky. A DONE member may be copied by value; source lifetime still applies.
   Do not reinitialize a lookup while its embedded member is used by a reader. */
enum kosdec_adx_status kosdec_adx_index_step(struct kosdec_adx_index *);
/* Use with stream_init(view_read_at,&member,member.bytes,repeat). Offsets/EOF
   are member-relative. Clamps positive requests to remaining member bytes and
   512 bytes; exact EOF makes no parent call. Invalid args/past-end offset or
   invalid parent results return INPUT_ERROR. Parent WAIT/early END propagate.
   View is immutable; parent/provider I/O may block. No retained destination. */
struct kosdec_adx_input_result kosdec_adx_view_read_at(
    void *view, uint32_t offset, uint8_t *dst, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
