/* Original bounded reader for Dreamcast-era CRI AFS archives. */
#ifndef KOSDEC_AFS_H
#define KOSDEC_AFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOSDEC_AFS_NAME_BYTES 32u
#define KOSDEC_AFS_METADATA_BYTES 16u
#define KOSDEC_AFS_SECTOR_BYTES 2048u

enum kosdec_afs_status {
    KOSDEC_AFS_OK, KOSDEC_AFS_EOF, KOSDEC_AFS_NOT_FOUND,
    KOSDEC_AFS_BAD_ARGUMENT, KOSDEC_AFS_INVALID, KOSDEC_AFS_TRUNCATED,
    KOSDEC_AFS_UNSUPPORTED, KOSDEC_AFS_IO_ERROR
};

/* Synchronous byte-positioned source. Return the number of bytes copied,
   zero for EOF or a negative value on error. Short reads are permitted.
   The provider must not retain the destination or reenter the archive. */
typedef ptrdiff_t (*kosdec_afs_read_at)(void *context, uint64_t position,
    void *destination, size_t capacity);

/* Caller-owned handles. Treat fields as read-only after admission. Keep the
   provider/context and immutable extent alive through all member reads. */
struct kosdec_afs {
    kosdec_afs_read_at read_at;
    void *context;
    uint64_t bytes;
    uint32_t count, names_offset;
};

struct kosdec_afs_member {
    const struct kosdec_afs *archive;
    uint32_t id, offset, bytes;
    bool has_name_table;
    char name[KOSDEC_AFS_NAME_BYTES + 1];
    uint8_t metadata[KOSDEC_AFS_METADATA_BYTES];
};

/* Validate all ordinal ranges and the optional trailing name table. Uses
   fixed scratch storage and borrows the source on success. No allocations. */
enum kosdec_afs_status kosdec_afs_open(struct kosdec_afs *archive,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes);
enum kosdec_afs_status kosdec_afs_get(const struct kosdec_afs *archive, uint32_t id,
    struct kosdec_afs_member *out);
/* Exact bytewise match, first ordinal on duplicate names. */
enum kosdec_afs_status kosdec_afs_find(const struct kosdec_afs *archive, const char *name,
    struct kosdec_afs_member *out);
/* Read no farther than the member's declared boundary. At that boundary a
   nonempty request returns EOF and zero used. Errors leave used at zero. */
enum kosdec_afs_status kosdec_afs_read(const struct kosdec_afs_member *member,
    uint32_t position, void *destination, size_t capacity, size_t *used);

#ifdef __cplusplus
}
#endif
#endif
