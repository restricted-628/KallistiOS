/* Original bounded reader for Dreamcast-era AFS archives. */
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
    KOSDEC_AFS_UNSUPPORTED, KOSDEC_AFS_IO_ERROR,
    KOSDEC_AFS_LOADING, KOSDEC_AFS_WAIT, KOSDEC_AFS_NO_SPACE, KOSDEC_AFS_BUSY
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
    uint32_t count, names_offset, name_records;
};

struct kosdec_afs_member {
    const struct kosdec_afs *archive;
    uint32_t id, offset, bytes;
    bool has_name_table; /* True when this member has a name record. */
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

/* Resident, ordinal-only partition access. Workspace and registry objects
   are caller-owned. Serialize operations and initialize partitions and
   registries to zero before first use. Metadata names remain available
   through the standalone archive API above. */
#define KOSDEC_AFS_PARTITION_IDS 256u
#define KOSDEC_AFS_PARTITION_MAX_MEMBERS 65536u
/* Opt-in response from a partition source: no bytes copied, retry later.
   Ordinary reads use a synchronous provider and treat negative values as errors. */
#define KOSDEC_AFS_READ_WAIT ((ptrdiff_t)-2)

struct kosdec_afs_range {
    uint32_t offset, bytes;
};

struct kosdec_afs_partition {
    struct kosdec_afs archive;
    struct kosdec_afs_range *entries;
    const char *filename;
    void *directory;
    struct kosdec_afs_partition *previous;
    enum kosdec_afs_status state;
    uint32_t capacity, loaded;
    unsigned used;
    bool have_header, bound;
    uint8_t scratch[512];
};

struct kosdec_afs_registry {
    struct kosdec_afs_partition *heads[KOSDEC_AFS_PARTITION_IDS];
};

struct kosdec_afs_sector_range {
    const char *filename;
    void *directory;
    uint32_t first_sector, sectors;
};

/* Workspace is a typed array of ranges. Returns zero for zero or unsupported
   counts. Info size reports the in-use array bytes for the bound head. */
size_t kosdec_afs_partition_work_size(uint32_t count);
enum kosdec_afs_status kosdec_afs_partition_info_size(const struct kosdec_afs_registry *registry,
    unsigned id, size_t *bytes);

/* Begin performs no I/O. The source may implement WAIT for cooperative
   loading. Each step makes at most one read of at most 512 bytes; the caller
   owns scheduling and retries. No partial table is published. A failed
   load is sticky. Reinitialize only when detached and all member borrows
   have ended. Keep filename, directory, workspace and source alive until
   then. NULL directory denotes the caller's default context. */
enum kosdec_afs_status kosdec_afs_partition_begin(struct kosdec_afs_partition *partition,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes, const char *filename,
    void *directory, struct kosdec_afs_range *entries, uint32_t capacity);
enum kosdec_afs_status kosdec_afs_partition_step(struct kosdec_afs_partition *partition);
enum kosdec_afs_status kosdec_afs_partition_status(const struct kosdec_afs_partition *partition);
/* Blocking load for sources that perform their own synchronous I/O. A WAIT
   response returns to the caller, who can continue with partition_step. */
enum kosdec_afs_status kosdec_afs_partition_load(struct kosdec_afs_partition *partition,
    kosdec_afs_read_at read_at, void *context, uint64_t bytes, const char *filename,
    void *directory, struct kosdec_afs_range *entries, uint32_t capacity);

/* Attach an admitted base to a free ID, or add an admitted overlay to an
   occupied ID. A partition object can be attached once. Pop unbinds the
   newest layer, leaving older layers intact. A member already returned
   retains its original source even if its partition is detached. */
enum kosdec_afs_status kosdec_afs_partition_bind(struct kosdec_afs_registry *registry,
    unsigned id, struct kosdec_afs_partition *partition);
enum kosdec_afs_status kosdec_afs_partition_add(struct kosdec_afs_registry *registry,
    unsigned id, struct kosdec_afs_partition *partition);
enum kosdec_afs_status kosdec_afs_partition_pop(struct kosdec_afs_registry *registry, unsigned id);
/* Look up the newest populated slot within the head's ordinal count,
   falling back through older layers for holes. Member reads use exact byte
   bounds. Sector queries require aligned starts and round length up. */
enum kosdec_afs_status kosdec_afs_partition_open(const struct kosdec_afs_registry *registry,
    unsigned partition_id, uint32_t member_id, struct kosdec_afs_member *out);
enum kosdec_afs_status kosdec_afs_partition_sector_range(const struct kosdec_afs_registry *registry,
    unsigned partition_id, uint32_t member_id, struct kosdec_afs_sector_range *out);

#ifdef __cplusplus
}
#endif
#endif
