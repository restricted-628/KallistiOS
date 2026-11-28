/* Bounded ordinal access to Dreamcast-era ACX archives. */
#ifndef KOSDEC_ACX_H
#define KOSDEC_ACX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum kosdec_acx_status {
    KOSDEC_ACX_OK, KOSDEC_ACX_EOF, KOSDEC_ACX_NOT_FOUND,
    KOSDEC_ACX_BAD_ARGUMENT, KOSDEC_ACX_INVALID, KOSDEC_ACX_TRUNCATED,
    KOSDEC_ACX_UNSUPPORTED, KOSDEC_ACX_IO_ERROR
};

/* Synchronous position-based source. Return copied bytes, zero for EOF or
   a negative value on error. Short reads are allowed. Borrow the destination
   only during the call. Serialize access if the provider uses a seek cursor. */
typedef ptrdiff_t (*kosdec_acx_read_at)(void *context, uint64_t position,
    void *destination, size_t capacity);

struct kosdec_acx {
    kosdec_acx_read_at read_at;
    void *context;
    uint64_t bytes;
    uint32_t count;
};

struct kosdec_acx_member {
    const struct kosdec_acx *archive;
    uint32_t id, offset, bytes;
};

/* Validate the whole ordinal table and publish only on success.
   No allocations. Keep the archive and immutable source alive for all views.
   Treat fields as read-only after admission. */
enum kosdec_acx_status kosdec_acx_open(struct kosdec_acx *archive,
    kosdec_acx_read_at read_at, void *context, uint64_t bytes);
enum kosdec_acx_status kosdec_acx_get(const struct kosdec_acx *archive,
    uint32_t id, struct kosdec_acx_member *out);
/* Read at a member-relative byte position. Requests clip at the member's
   declared size. A nonempty request at the boundary returns EOF. used is
   zero on failure. Payload bytes have no codec or alignment requirements. */
enum kosdec_acx_status kosdec_acx_read(const struct kosdec_acx_member *member,
    uint32_t position, void *destination, size_t capacity, size_t *used);

#ifdef __cplusplus
}
#endif
#endif
