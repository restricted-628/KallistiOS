/* KallistiOS ##version##

   dc/fs_iso9660.h
   (c)2000-2001 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/fs_iso9660.h
    \brief   ISO9660 (CD-ROM) filesystem driver.
    \ingroup gdrom

    This driver implements support for reading files from a CD-ROM or CD-R in
    the Dreamcast's disc drive. This filesystem mounts itself on /cd.

    This driver supports Rock Ridge, thanks to Andrew Kieschnick. The driver
    also supports the Joliet extensions thanks to Bero.

    The implementation was originally based on a simple ISO9660 implementation
    by Marcus Comstedt.

    \author Megan Potter
    \author Andrew Kieschnick
    \author Bero
*/

#ifndef __DC_FS_ISO9660_H
#define __DC_FS_ISO9660_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <dc/cdrom.h>
#include <kos/fs.h>

/** \addtogroup gdrom
    @{
*/

/** \brief Physical command backend used by the ISO9660 filesystem. */
typedef enum fs_iso9660_backend {
    /** \brief Use the Dreamcast BIOS GD-ROM command server (default). */
    FS_ISO9660_BACKEND_BIOS = 0,
    /** \brief Use KOS's direct SPI/Holly GD-ROM driver. */
    FS_ISO9660_BACKEND_DIRECT
} fs_iso9660_backend_t;

/** \brief Select the physical backend used by `/cd`.

    The BIOS backend remains the default. The direct backend performs disc
    probing, TOC discovery, cached metadata reads, synchronous file reads, and
    asynchronous DMA chains without the BIOS GD-ROM command server.

    Selection must be made before the first operation that mounts or reads
    `/cd`. Once a mount attempt has begun, the backend is locked for the
    lifetime of the filesystem driver so open descriptors and in-flight
    requests can never straddle two controller implementations. Re-selecting
    the already-active backend is permitted.

    Staged drive-buffer sessions use BIOS streaming commands or direct SPI
    `CD_READ2` according to this selection. Pickup preseek hints use direct
    `CD_SEEK` when direct mode is selected. The first `/cd` mount attempt
    starts KOS's shared media monitor; it follows this selection and reports
    its active sampler through `cdrom_drive_state_t.backend`.

    \param backend          Backend to select.

    \retval 0              Backend selected, or already selected.
    \retval -1             Invalid backend (`EINVAL`) or selection is already
                           locked to another backend (`EBUSY`).
*/
int fs_iso9660_set_backend(fs_iso9660_backend_t backend);

/** \brief Return the currently selected ISO9660 physical backend. */
fs_iso9660_backend_t fs_iso9660_get_backend(void);

/** \defgroup iso9660_file_flags ISO9660 directory record flags
    \brief ISO9660 directory record flags exposed by the filesystem driver.
    \ingroup gdrom

    These values correspond directly to the flags byte in an ISO9660
    directory record.

    @{ */
#define ISO9660_FILE_HIDDEN          0x01 /**< \brief Hidden file. */
#define ISO9660_FILE_DIRECTORY       0x02 /**< \brief Directory. */
#define ISO9660_FILE_ASSOCIATED      0x04 /**< \brief Associated file. */
#define ISO9660_FILE_RECORD          0x08 /**< \brief Record format is specified. */
#define ISO9660_FILE_PROTECTED       0x10 /**< \brief Protected file. */
#define ISO9660_FILE_MULTI_EXTENT    0x80 /**< \brief Not the final extent. */
/** @} */

/** \brief ISO9660-specific information about an open file or directory. */
typedef struct iso9660_file_info {
    uint32_t extent_lba;       /**< \brief First extent's logical block address. */
    uint32_t extent_fad;       /**< \brief First extent's GD-ROM FAD. */
    uint32_t size;             /**< \brief Recorded size in bytes. */
    uint32_t sector_count;     /**< \brief Recorded size rounded up to sectors. */
    uint8_t  flags;            /**< \brief Combination of ISO9660_FILE_* flags. */
    uint8_t  ext_attr_length;  /**< \brief Extended attribute length in blocks. */
    uint8_t  file_unit_size;   /**< \brief Interleaved file unit size. */
    uint8_t  interleave_gap;   /**< \brief Interleaved gap size. */
} iso9660_file_info_t;

/** \brief Maximum RAM retained by installed directory prefetch snapshots.

    Snapshots use a managed least-recently-used cache. Prefetching a directory
    larger than this limit fails with `EFBIG`; adding a new snapshot evicts old
    snapshots as needed to stay within the limit. Each outstanding prefetch
    also owns a transient, sector-rounded buffer of up to this size until it
    completes; that memory is not included in the resident-cache limit.
*/
#define ISO9660_DIRECTORY_PREFETCH_BYTES (128 * 1024)

/** \brief ISO9660 metadata-cache measurement counters.

    Counters reset when the mounted medium changes or iso_reset() clears the
    cache. Snapshot entry and byte fields report current residency rather than
    cumulative totals.
*/
typedef struct iso9660_cache_stats {
    uint64_t metadata_sector_hits;    /**< \brief Inode-cache sector hits. */
    uint64_t metadata_sector_misses;  /**< \brief Inode-cache sector reads. */
    uint64_t metadata_sector_evictions; /**< \brief Occupied inode blocks replaced. */
    uint64_t directory_snapshot_hits; /**< \brief Lookups served by snapshots. */
    uint64_t directory_snapshot_misses; /**< \brief Lookups without a snapshot. */
    uint64_t directory_snapshot_evictions; /**< \brief Snapshots evicted by budget. */
    size_t directory_snapshot_entries; /**< \brief Resident directory snapshots. */
    size_t directory_snapshot_bytes;   /**< \brief RAM used by snapshots. */
} iso9660_cache_stats_t;

/** \brief Request ISO9660-specific information with fs_ioctl(). */
#define IOCTL_ISO9660_GET_FILE_INFO 0x49534f30 /* "ISO0" */

/** \brief Get ISO9660-specific information for an open descriptor.

    The descriptor must refer to a file or directory opened from the ISO9660
    filesystem mounted at `/cd`.

    \param fd       Open ISO9660 file or directory descriptor.
    \param info     Output structure to fill.

    \retval 0       On success.
    \retval -1      On failure, with errno set.
*/
int fs_iso9660_get_file_info(file_t fd, iso9660_file_info_t *info);

/** \brief Get ISO9660-specific information by path.

    The path is resolved through the normal KOS VFS and must refer to a file or
    directory on the ISO9660 filesystem mounted at `/cd`.

    \param path     Path to an ISO9660 file or directory.
    \param info     Output structure to fill.

    \retval 0       On success.
    \retval -1      On failure, with errno set.
*/
int fs_iso9660_get_path_info(const char *path, iso9660_file_info_t *info);

/** \brief Read the current ISO9660 metadata-cache measurements.

    \param stats    Destination for a coherent counter snapshot.

    \retval 0      On success.
    \retval -1     If `stats` is NULL, with errno set to `EINVAL`.
*/
int fs_iso9660_get_cache_stats(iso9660_cache_stats_t *stats);

/** \brief Asynchronously retain an open directory's complete record image.

    The descriptor must refer to a directory on `/cd`. Its ISO directory extent
    is read through the GD-ROM request engine in bounded 32 KiB commands,
    then installed in a managed LRU snapshot cache before completion becomes
    visible. Normal path lookup, open, and stat operations automatically use
    resident snapshots and avoid metadata-sector I/O for that directory.
    Because the retained image is driver-owned rather than caller-visible,
    request progress is reported by `io_completed_bytes` / `io_bytes`; the
    caller-visible data-byte counters remain zero.

    The descriptor is retained internally, so the caller may close it after
    this function returns. Other operations on the same open file description
    return `EBUSY` until completion. Media change or cache reset prevents the
    completed image from being installed.

    \param directory       Open ISO9660 directory descriptor.
    \param timeout         Whole-request timeout in milliseconds after its
                           first physical command starts, or zero for none.
    \param callback        Optional completion callback in thread context.
    \param callback_data   User data passed to the callback.

    \return                A GD-ROM request, or NULL with errno set.
*/
cdrom_request_t *fs_iso9660_prefetch_directory_async(
    file_t directory, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief Synchronously retain an open directory's complete record image.

    This is the blocking form of fs_iso9660_prefetch_directory_async().

    \param directory       Open ISO9660 directory descriptor.

    \retval 0              Snapshot installed successfully.
    \retval -1             Prefetch failed, with errno set.
*/
int fs_iso9660_prefetch_directory(file_t directory);

/** \brief Read whole ISO file sectors synchronously using direct GD DMA.

    This is the blocking form of fs_iso9660_read_direct_async() and has the
    same hard alignment contract. It never silently falls back to a bounced or
    cached read.

    \param fd              Open regular file on the ISO9660 `/cd` filesystem.
    \param buffer          32-byte-aligned destination with room for
                           `sector_count * 2048` bytes.
    \param sector_count    Maximum number of sectors to read.

    \return                File data bytes read, zero at EOF, or -1 with errno
                           set. The return value may be shorter than the
                           requested sector span at EOF.
*/
ssize_t fs_iso9660_read_direct(file_t fd, void *buffer, size_t sector_count);

/** \brief Read whole ISO file sectors asynchronously using direct GD DMA.

    The file position and destination must both be sector/DMA aligned: the
    descriptor position must be a multiple of 2,048 bytes and `buffer` must be
    32-byte aligned. The request reads at most the sectors remaining in the
    file. If the recorded file ends within the final sector, bytes after EOF in
    that sector are zero-filled before completion is reported. Request status
    distinguishes the caller's requested size, actual file data, and the
    sector-rounded physical transfer size; a request extending past EOF
    therefore completes successfully with fewer `data_bytes` than
    `requested_bytes`. Large transfers are split into bounded physical
    commands and requeued between them so other GD-ROM requests can run.

    The descriptor is retained internally through driver finalization and, if
    supplied, until the completion callback returns, so the caller may close
    its descriptor while the request runs. No other read or seek may be issued
    on the same open file description until completion. The buffer must not be
    accessed until completion. On any terminal state, its first
    `status.completed_bytes` are valid; subsequent bytes have undefined
    contents. The descriptor advances only after complete success and remains
    unchanged after cancellation, timeout, or error, allowing the caller to
    consume the valid prefix and seek explicitly if desired.

    \param fd              Open regular file on the ISO9660 `/cd` filesystem.
    \param buffer          Destination with room for `sector_count * 2048`
                           bytes.
    \param sector_count    Maximum number of sectors to read.
    \param timeout         Timeout after the GD command starts, in milliseconds,
                           or zero for no timeout.
    \param callback        Optional completion callback in thread context.
    \param callback_data   User data passed to the callback.

    \return                A GD-ROM request, or NULL with errno set.
*/
cdrom_request_t *fs_iso9660_read_direct_async(
    file_t fd, void *buffer, size_t sector_count, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief Read arbitrary ISO file bytes asynchronously.

    This is the nonblocking counterpart to fs_read() for regular files on
    `/cd`. Any file position, destination alignment, and byte count are
    accepted. Whole aligned portions use direct GD DMA; partial sectors and
    misaligned destinations are copied through a bounded driver workspace.
    The 32 KiB workspace is allocated only on the first request that actually
    needs a bounce segment; aligned whole-sector requests do not reserve it.

    One logical request may contain several bounded physical commands. It is
    requeued between commands so other GD-ROM requests can run, while status,
    cancellation, timeout, and callback behavior remain operation-wide. The
    timeout starts with the first physical command and includes time spent
    queued between later commands.

    On success, the descriptor advances by `status.data_bytes`. Bytes in the
    caller's requested range beyond EOF are left untouched. On cancellation,
    timeout, or error, the descriptor position remains unchanged. After any
    terminal state, the first `status.completed_bytes` in the destination are
    valid and later bytes have undefined contents; this permits explicit
    prefix recovery without making descriptor advancement partial. The caller
    must not access the destination until the request reaches a terminal
    state.

    \param fd              Open regular file on the ISO9660 `/cd` filesystem.
    \param buffer          Destination with room for `bytes` bytes, or NULL
                           when `bytes` is zero.
    \param bytes           Maximum file bytes to read.
    \param timeout         Whole-request timeout in milliseconds after its
                           first physical command starts, or zero for none.
    \param callback        Optional completion callback in thread context.
    \param callback_data   User data passed to the callback.

    \return                A GD-ROM request, or NULL with errno set.
*/
cdrom_request_t *fs_iso9660_read_async(
    file_t fd, void *buffer, size_t bytes, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief Start file-backed staged GD-ROM read-ahead.

    The descriptor position must be sector aligned. The session stages at most
    `sector_count` sectors beginning at that position and retains the open file
    description until the session becomes terminal. Use
    cdrom_stream_session_transfer_async() for each application-directed RAM
    transfer. Each successful transfer consumes its useful file bytes; the
    final physical-sector tail after EOF is zero-filled.

    This opt-in staged API requires the application to submit transfers after
    the session becomes ready. `idle_timeout` bounds that exclusive drive/G1
    ownership; expiry aborts the stream and makes the session timed out.

    The descriptor remains busy for the entire session. When the session ends,
    its position advances by the useful bytes from fully completed transfer
    requests, including if a later transfer or the session itself is
    cancelled. At EOF this function fails with `ENODATA` rather than creating
    an empty drive session.

    \param fd              Open regular file on the ISO9660 `/cd` filesystem.
    \param sector_count    Maximum number of sectors to stage.
    \param start_timeout   Startup timeout in milliseconds, or zero for none.
    \param idle_timeout    Required nonzero ready-idle timeout in milliseconds.

    \return                A staged-stream session, or NULL with errno
                           set.
*/
cdrom_stream_session_t *fs_iso9660_stream_start(
    file_t fd, size_t sector_count, uint32_t start_timeout,
    uint32_t idle_timeout);

/** \brief Move the GD-ROM pickup to an ISO file's next read position.

    This is a nonblocking scheduling hint corresponding to the descriptor's
    current byte position. It does not read data or change that position. The
    descriptor is retained through completion; reads and seeks on the same
    open file description return EBUSY while the request is active. Under the
    direct backend, a zero timeout selects the driver's bounded four-second
    default rather than creating an unbounded direct command.

    \param fd              Open regular file on the ISO9660 `/cd` filesystem.
    \param timeout         Timeout after the GD command starts, in milliseconds,
                           or zero for no timeout.
    \param callback        Optional completion callback in thread context.
    \param callback_data   User data passed to the callback.

    \return                A GD-ROM request, or NULL with errno set.
*/
cdrom_request_t *fs_iso9660_preseek_async(
    file_t fd, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief  Reset the internal ISO9660 cache.

    This function resets the cache of the ISO9660 driver, breaking connections
    to all files. This generally assumes that a new disc has been or will be
    inserted.

    \retval 0               On success.
*/
int iso_reset(void);

/* \cond */
void fs_iso9660_init(void);
void fs_iso9660_shutdown(void);
/* \endcond */

/** @} */

__END_DECLS

#endif  /* __DC_FS_ISO9660_H */
