/* KallistiOS ##version##

   dc/vmufs.h
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/vmufs.h
    \brief   Low-level VMU filesystem driver.
    \ingroup vfs_vmu

    The VMU filesystem driver mounts itself on /vmu of the VFS. Each memory card
    has its own subdirectory off of that directory (i.e, /vmu/a1 for slot 1 of
    the first controller). VMUs themselves have no subdirectories, so the driver
    itself is fairly simple.

    Files on a VMU must be multiples of 512 bytes in size, and should have a
    header attached so that they show up in the BIOS menu.

    \author Megan Potter
    \see    dc/vmu_pkg.h
    \see    dc/fs_vmu.h
    \see    dc/vmufs_meta.h
*/

#ifndef __DC_VMUFS_H
#define __DC_VMUFS_H

#include <stdbool.h>
#include <stdint.h>
#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs_meta.h>

/** \addtogroup vfs_vmu
    @{
*/

/* Notes about the "dirty" field on vmu_dir_t :)

   This byte should always be zero when written out to the VMU. What this
   lets us do, though, is conserve on flash writes. If you only want to
   modify one single file (which is the standard case) then re-writing all
   of the dir blocks is a big waste. Instead, you should set the dirty flag
   on the in-mem copy of the directory, and writing it back out will only
   flush the containing block back to the VMU, setting it back to zero
   in the process. Loaded blocks should always have zero here (though we
   enforce that in the code to make sure) so it will be non-dirty by
   default.
 */


/* ****************** Low level functions ******************** */

/** \brief  Fill in the date on a vmu_dir_t for writing.

    \param  d               The directory to fill in the date on.
*/
void vmufs_dir_fill_time(vmu_dir_t *d);

/** \brief  Reads a selected VMU's root block.

    This function assumes the mutex is held.

    \param  dev             The VMU to read from.
    \param  root_buf        A buffer to hold the root block. You must allocate
                            this yourself before calling.
    \retval -1              On failure.
    \retval 0               On success.
*/
int vmufs_root_read(maple_device_t *dev, vmu_root_t *root_buf);

/** \brief  Writes a selected VMU's root block.

    This function assumes the mutex is held.

    \param  dev             The VMU to write to.
    \param  root_buf        The root block to write.
    \retval -1              On failure.
    \retval 0               On success.
*/
int vmufs_root_write(maple_device_t *dev, vmu_root_t *root_buf);

/** \brief  Given a VMU's root block, return the amount of space in bytes
            required to hold its directory.

    \param  root_buf        The root block to check.
    \return                 The amount of space, in bytes, needed.
*/
int vmufs_dir_blocks(vmu_root_t *root_buf);

/** \brief  Given a VMU's root block, return the amount of space in bytes
            required to hold its FAT.

    \param  root_buf        The root block to check.
    \return                 The amount of space, in bytes, needed.
*/
int vmufs_fat_blocks(vmu_root_t *root_buf);

/** \brief  Given a selected VMU's root block, read its directory.

    This function reads the directory of a given VMU root block. It assumes the
    mutex is held. There must be at least the number of bytes returned by
    vmufs_dir_blocks() available in the buffer for this to succeed.

    \param  dev             The VMU to read.
    \param  root_buf        The VMU's root block.
    \param  dir_buf         The buffer to hold the directory. You must have
                            allocated this yourself.
    \return                 0 on success, <0 on failure.
*/
int vmufs_dir_read(maple_device_t *dev, vmu_root_t *root_buf,
                   vmu_dir_t *dir_buf);

/** \brief  Given a selected VMU's root block and dir blocks, write the dirty
            dir blocks back to the VMU. Assumes the mutex is held.

    \param  dev             The VMU to write to.
    \param  root            The VMU's root block.
    \param  dir_buf         The VMU's directory structure.
    \return                 0 on success, <0 on failure.
*/
int vmufs_dir_write(maple_device_t *dev, vmu_root_t *root,
                    vmu_dir_t *dir_buf);

/** \brief Given a selected VMU's root block, read its FAT.

    This function reads the FAT of a VMU, given its root block. It assumes the
    mutex is held. There must be at least the number of bytes returned by
    vmufs_fat_blocks() available in the buffer for this to succeed.

    \param  dev             The VMU to read from.
    \param  root            The VMU's root block.
    \param  fat_buf         The buffer to store the FAT into. You must
                            pre-allocate this.
    \return                 0 on success, <0 on failure.
*/
int vmufs_fat_read(maple_device_t *dev, vmu_root_t *root, uint16_t *fat_buf);

/** \brief  Given a selected VMU's root block and its FAT, write the FAT blocks
            back to the VMU.

    This function assumes the mutex is held.

    \param  dev             The VMU to write to.
    \param  root            The VMU's root block.
    \param  fat_buf         The buffer to write to the FAT.
    \return                 0 on success, <0 on failure.
*/
int vmufs_fat_write(maple_device_t *dev, vmu_root_t *root, uint16_t *fat_buf);

/** \brief  Given a previously-read directory, locate a file by filename.

    \param  root            The VMU root block.
    \param  dir             The VMU directory.
    \param  fn              The file to find (only checked up to 12 chars).
    \return                 The index into the directory array on success, or
                            <0 on failure.
*/
int vmufs_dir_find(vmu_root_t *root, vmu_dir_t *dir, const char *fn);

/** \brief  Given a previously-read directory, add a new dirent to the dir.

    Another file with the same name should not exist (delete it first if it
    does). This function will not check for dupes!

    \param  root            The VMU root block.
    \param  dir             The VMU directory.
    \param  newdirent       The new entry to add.
    \return                 0 on success, or <0 on failure. */
int vmufs_dir_add(vmu_root_t *root, vmu_dir_t *dir, vmu_dir_t *newdirent);

/** \brief  Given a pointer to a directory struct and a previously loaded FAT,
            load the indicated file from the VMU.

    An appropriate amount of space must have been allocated previously in the
    buffer. Assumes the mutex is held. New code should prefer
    vmufs_file_read_ex(), which can distinguish user-data blocks from metadata
    using the root geometry.

    \param  dev             The VMU to read from.
    \param  fat             The FAT of the VMU.
    \param  dirent          The entry to read.
    \param  outbuf          A buffer to write the data into. You must allocate
                            this yourself with the appropriate amount of space.
    \return                 0 on success, <0 on failure.
*/
int vmufs_file_read(maple_device_t *dev, uint16_t *fat, vmu_dir_t *dirent, void *outbuf);

/** \brief Read a file after validating its complete FAT chain and geometry.

    Unlike vmufs_file_read(), this entry point uses the supplied root block to
    constrain every FAT index to the user-data region. The complete chain is
    resolved before the first device read, so invalid metadata cannot select a
    metadata block and leaves \p outbuf untouched.

    The caller must hold the VMU filesystem mutex and provide room for exactly
    `dirent->filesize * VMUFS_BLOCK_SIZE` bytes.

    \param dev      The VMU to read from.
    \param root     Its validated filesystem root block.
    \param fat      Its loaded FAT.
    \param dirent   Directory entry identifying the file.
    \param outbuf   Caller-owned output buffer.
    \retval 0       The complete file was read.
    \retval -1      Arguments, geometry, or the FAT chain were invalid.
    \retval -2      A device block read failed.
*/
int vmufs_file_read_ex(maple_device_t *dev, const vmu_root_t *root,
                       const uint16_t *fat, const vmu_dir_t *dirent,
                       void *outbuf);

/** \brief  Given a pointer to a mostly-filled directory struct and a previously
            loaded directory and FAT, write the indicated file to the VMU.

    The named file should not exist in the directory already. The directory and
    FAT will _not_ be sync'd back to the VMU, this must be done manually.
    Allocation is all-or-nothing and the whole supplied filesystem snapshot is
    validated before any data block is written. Assumes the mutex is held.

    \param  dev             The VMU to write to.
    \param  root            The VMU root block.
    \param  fat             The FAT of the VMU.
    \param  dir             The directory of the VMU.
    \param  newdirent       The new entry to write.
    \param  filebuf         The new file data.
    \param  size            The size of the file in blocks (512-bytes each).
    \return                 0 on success, <0 on failure.
*/
int vmufs_file_write(maple_device_t *dev, vmu_root_t *root, uint16_t *fat,
                     vmu_dir_t *dir, vmu_dir_t *newdirent, void *filebuf, int size);

/** \brief  Given a previously-read FAT and directory, delete the named file.

    No changes are made to the VMU itself, just the in-memory structs. The whole
    supplied filesystem snapshot is validated before its chain is released.

    \param  root            The VMU root block.
    \param  fat             The FAT to be modified.
    \param  dir             The directory to be modified.
    \param  fn              The file name to be deleted.
    \retval 0               On success.
    \retval -1              If fn is not found.
*/
int vmufs_file_delete(vmu_root_t *root, uint16_t *fat, vmu_dir_t *dir, const char *fn);

/** \brief  Given a previously-read FAT, return the number of blocks available
            to write out new file data.

    \param  root            The VMU root block.
    \param  fat             The FAT to be examined.
    \return                 The number of blocks available.
*/
int vmufs_fat_free(vmu_root_t *root, uint16_t *fat);

/** \brief  Given a previously-read directory, return the number of dirents
            available for new files.

    \param  root            The VMU root block.
    \param  dir             The directory in question.
    \return                 The number of entries available.
*/
int vmufs_dir_free(vmu_root_t *root, vmu_dir_t *dir);

/** \brief  Lock the vmufs mutex.

    This should be done before you attempt any low-level ops.

    \retval 0               On success (no error conditions defined).
*/
int vmufs_mutex_lock(void);

/** \brief  Unlock the vmufs mutex.

    This should be done once you're done with any low-level ops.

    \retval 0               On success (no error conditions defined).
*/
int vmufs_mutex_unlock(void);


/* ****************** Higher level functions ******************** */

/** \brief  Read the directory from a VMU.

    The output buffer will be allocated for you using malloc(), and the number
    of entries will be returned. On failure, outbuf will not contain a dangling
    buffer that needs to be freed (no further action required).

    \param  dev             The VMU to read from.
    \param  outbuf          A buffer that will be allocated where the directory
                            data will be placed.
    \param  outcnt          The number of entries in outbuf.
    \return                 0 on success, or <0 on failure. */
int vmufs_readdir(maple_device_t *dev, vmu_dir_t **outbuf, int *outcnt);

/** \brief Read one file's directory metadata without loading its data.

    The returned entry contains the stored block count, type, timestamp,
    copy-protection value, and header offset. Its in-memory dirty marker is
    always cleared.

    \param dev   VMU to inspect.
    \param fn    File name.
    \param info  Destination directory entry.
    \retval 0    Metadata copied successfully.
    \retval -1   Invalid arguments, missing file, or device failure.
*/
int vmufs_get_file_info(maple_device_t *dev, const char *fn,
                        vmu_dir_t *info);

/** \brief Inspect a memory card's filesystem and allocation state.

    A readable card returns 0 even when its filesystem is unformatted,
    corrupt, degraded, or unsupported; inspect `info->state` for that result.
    Missing root magic is classified as unformatted, including the deliberate
    invalid-root state left by an interrupted format. A degraded volume has
    only orphaned allocation blocks and remains safe for ordinary mutations,
    although those blocks consume capacity until repaired or reformatted.

    The complete result is published only after all required metadata reads
    and validation succeed. This synchronous query does not start the optional
    VMU request workers.

    \param dev   Memory card to inspect.
    \param info  Destination for the complete volume summary.
    \retval 0    Card read and classified; inspect `info->state`.
    \retval -1   Invalid arguments, allocation failure, or device I/O failure.
*/
int vmufs_get_volume_info(maple_device_t *dev, vmufs_volume_info_t *info);

/** \brief  Read a file from the VMU.

    The output buffer will be allocated for you using malloc(), and the size of
    the file will be returned.  On failure, outbuf will not contain a dangling
    buffer that needs to be freed (no further action required).

    \param  dev             The VMU to read from.
    \param  fn              The name of the file to read.
    \param  outbuf          A buffer that will be allocated where the file data
                            will be placed.
    \param  outsize         Storage for the size of the file, in bytes.
    \return                 0 on success, or <0 on failure.
*/
int vmufs_read(maple_device_t *dev, const char *fn, void **outbuf, int *outsize);

/** \brief Read an exact file-relative range of 512-byte blocks.

    The complete FAT chain is resolved and validated before the destination is
    modified. The range must fit entirely inside the file. Each successful raw
    block response is validated before its 512 bytes are copied to \p outbuf.

    \param dev          VMU containing the file.
    \param fn           File name.
    \param first_block  First file-relative block to read.
    \param outbuf       Destination for \p block_count contiguous blocks.
    \param block_count  Number of blocks to read; zero is permitted.
    \retval 0           Complete range read successfully.
    \retval -1          Invalid range, metadata, response, or device failure.
*/
int vmufs_read_blocks(maple_device_t *dev, const char *fn,
                      size_t first_block, void *outbuf,
                      size_t block_count);

/** \brief Replace an exact file-relative range of 512-byte blocks.

    Ordinary files are replaced copy-on-write: unmodified blocks are copied
    into a new chain, the new FAT is committed, the directory switches chains,
    and the old chain is reclaimed last. The file's type, timestamp, name,
    copy protection, size, and header offset are preserved.

    An executable file cannot use that ordering because both the old and new
    images must begin at physical block zero. KOS reads every original target
    block before the first write, accepts cancellation only before that write,
    verifies each replacement, and attempts rollback after an I/O failure.
    Power loss or removal after the first executable block write can still
    leave a partially rewritten executable; the media layout has no journal.

    \param dev          VMU containing the file.
    \param fn           File name.
    \param first_block  First file-relative block to replace.
    \param inbuf        Source for \p block_count complete blocks.
    \param block_count  Number of blocks to replace; zero is permitted.
    \retval 0           Rewrite completed.
    \retval -1          No file data was deliberately changed.
    \retval -2          An executable rewrite or rollback became uncertain.
*/
int vmufs_rewrite_blocks(maple_device_t *dev, const char *fn,
                         size_t first_block, const void *inbuf,
                         size_t block_count);

/** \brief  Read a file from the VMU, using a pre-read dirent.

    This function is faster to use than vmufs_read() if you already have done
    the lookup, since it won't need to do that.

    \param  dev             The VMU to read from.
    \param  dirent          The entry to read.
    \param  outbuf          A buffer that will be allocated where the file data
                            will be placed.
    \param  outsize         Storage for the size of the file, in bytes.
    \return                 0 on success, <0 on failure.
*/
int vmufs_read_dirent(maple_device_t *dev, vmu_dir_t *dirent, void **outbuf, int *outsize);

/* Flags for vmufs_write */
#define VMUFS_OVERWRITE 1   /**< \brief Overwrite existing files */
#define VMUFS_VMUGAME   2   /**< \brief This file is a VMU game */
#define VMUFS_NOCOPY    4   /**< \brief Set the no-copy flag */

/** \brief Write a file to the VMU.

    If the named file already exists, then the function checks 'flags'. If
    VMUFS_OVERWRITE is set, replacement uses copy-on-write: new data and a FAT
    containing both chains are committed before the directory selects the new
    file, and the old chain is reclaimed last. Enough currently free space must
    exist for the complete replacement in addition to the old file.

    New files commit data first, FAT second, and directory last. Interrupted
    cleanup can leak allocated blocks, but no committed directory entry is
    deliberately made to reference incomplete or already-freed data. Before
    changing the card, KOS validates all directory and FAT ownership; unsafe
    corruption is rejected with `errno` set to `EILSEQ`.

    Executable images require one contiguous free prefix beginning at block
    zero. Fragmented total free space is not sufficient.

    Calls are serialized against other VMUFS operations by the filesystem
    mutex. Direct block access or another filesystem implementation operating
    on the same card is outside that protection and must be excluded by the
    application.

    \param  dev             The VMU to write to.
    \param  fn              The filename to write.
    \param  inbuf           The data to write to the file.
    \param  insize          The size of the file in bytes.
    \param  flags           Flags for the write (i.e, VMUFS_OVERWRITE,
                            VMUFS_VMUGAME, VMUFS_NOCOPY).
    \retval 0               Success.
    \retval -7              Insufficient valid allocation space.
    \retval -8              Replacement committed, but the old chain could not
                            be reclaimed and may remain orphaned.
    \return                 Another negative value on failure.
*/
int vmufs_write(maple_device_t *dev, const char *fn, void *inbuf, int insize, int flags);

/** \brief  Delete a file from the VMU.

    The complete filesystem is validated first. The directory entry is removed
    before its blocks are released, so failed cleanup can leak space but cannot
    make another live entry reference blocks that have already been reused.

    Calls are serialized against other VMUFS operations by the filesystem
    mutex. Direct block access or another filesystem implementation operating
    on the same card is outside that protection and must be excluded by the
    application.

    \retval 0               On success.
    \retval -1              If the file is not found.
    \retval -2              On other failure.
*/
int vmufs_delete(maple_device_t *dev, const char *fn);

/** \brief Delete several files with directory-before-FAT commit ordering.

    Every name must be unique, valid, and present, and the complete filesystem
    must pass the mutation safety gate before the first write. All affected
    directory blocks are removed before their FAT chains are released. If a
    directory write is not acknowledged, only entries from earlier,
    positively acknowledged blocks are eligible for FAT cleanup; therefore a
    failure can leak blocks but cannot free storage still owned by a confirmed
    live entry.

    Cancellation is accepted before the first directory commit. Once that
    barrier is crossed, the operation finishes the planned directory writes
    and cleanup. On any post-barrier failure, inspect \p result and re-query
    names when `directory_state_uncertain` is true.

    \param dev         VMU containing the files.
    \param filenames   Array of \p file_count null-terminated names.
    \param file_count  Number of names; zero is permitted.
    \param result      Detailed outcome, initialized on every accepted call.
    \retval 0          Every file removed and allocation cleanup acknowledged.
    \retval -1         Failure before the commit barrier; no confirmed removal.
    \retval -2         Commit began but completion or cleanup was incomplete.
*/
int vmufs_delete_files(
    maple_device_t *dev, const char *const *filenames, size_t file_count,
    vmufs_delete_result_t *result);

/** \brief Reclaim only allocation blocks unreachable from every live file.

    The complete filesystem must either be valid or contain orphan allocation
    as its only inconsistency. Cross-links, cycles, duplicate names, malformed
    chains, and unsafe geometry are rejected without a write. The operation
    changes only the FAT and never guesses an orphan's former owner.

    \param dev     Memory card to repair.
    \param result  Detailed outcome, initialized on every accepted call.
    \retval 0      The FAT is valid and any orphan cleanup was acknowledged.
    \retval -1     Validation, allocation, or device I/O failed.
*/
int vmufs_repair(maple_device_t *dev, vmufs_repair_result_t *result);

/** \brief Rename a file, replacing an existing destination safely.

    File contents, timestamp, type, copy protection, and header offset are
    preserved. The complete filesystem is validated before mutation. When a
    destination exists, its directory entry is removed before its FAT chain is
    released, so interruption can leak blocks but cannot cross-link live files.
    If the entries occupy different directory blocks, interruption after the
    first commit can leave the source under its old name with the destination
    removed.

    \param dev       VMU containing both names.
    \param old_name  Existing file name.
    \param new_name  New file name of at most 12 bytes.
    \retval 0        Rename completed or both names were identical.
    \retval -1       Failure before or during the directory commit.
    \retval -2       Rename committed, but replaced blocks may be orphaned.
*/
int vmufs_rename(maple_device_t *dev, const char *old_name,
                 const char *new_name);

/** \brief Update safe directory attributes without rewriting file data.

    The complete filesystem is validated before mutation. The header offset
    must identify a block inside the existing file. Only the containing
    directory block is committed; the file name, type, timestamp, allocation,
    and contents are preserved.

    \param dev         VMU containing the file.
    \param fn          Existing file name of at most 12 bytes.
    \param attributes  New copy-protection and header-offset values.
    \retval 0          Attributes committed or already identical.
    \retval -1         Invalid arguments, metadata, or device failure.
*/
int vmufs_set_file_attributes(
    maple_device_t *dev, const char *fn,
    const vmufs_file_attributes_t *attributes);

/** \brief Format a standard 128 KiB memory card.

    Quick format invalidates the current root, clears the directory, writes a
    canonical FAT, and publishes the new root last. Full format additionally
    clears every user and unused data block. Once the invalid root is written,
    cancellation is not applicable and interruption leaves the card
    detectably unformatted rather than exposing partially replaced geometry.

    This operation destroys every file on the card.

    \param dev      Memory card to format.
    \param options  Volume color, timestamp, and icon metadata.
    \param mode     Quick or full format.
    \retval 0       Format and read-back verification succeeded.
    \retval -1      Failure; errno describes the argument or device error.
*/
int vmufs_format(maple_device_t *dev,
                 const vmufs_format_options_t *options,
                 vmufs_format_mode_t mode);

/** \brief Safely pack files to maximize low-block contiguous free space.

    Each file move is copy-on-write: target data and a staging FAT are written
    before the directory switches chains, and the old chain is released last.
    Dependency cycles use blocks that remain free in the final layout as
    temporary storage. If no interruption-safe schedule exists, the operation
    fails with `ENOSPC` before changing the card.

    A successful result keeps an executable at block zero and packs ordinary
    files from the high end downward. At every completed block-write boundary,
    each directory entry selects either its complete old file or complete new
    file. Direct raw access to the same card must be excluded by the caller.

    \param dev      Memory card to defragment.
    \retval 0       Defragmentation completed or was unnecessary.
    \retval -1      Failure; errno is `ENOSPC`, `EILSEQ`, `ENOMEM`, or `EIO`.
*/
int vmufs_defragment(maple_device_t *dev);

/** \defgroup vfs_vmu_requests Asynchronous VMU filesystem requests
    \brief Lazy queued transactions with progress and safe cancellation.
    \ingroup vfs_vmu

    The request and callback workers are created together on the first valid
    asynchronous submission. Applications using only synchronous VMU access
    therefore reserve no request-worker stacks. Callbacks run outside both the
    VMU storage worker and interrupt context.

    @{ */

typedef struct vmufs_request vmufs_request_t;

typedef enum vmufs_request_operation {
    VMUFS_REQUEST_WRITE,       /**< \brief Transactional file save. */
    VMUFS_REQUEST_DELETE,      /**< \brief Transactional file deletion. */
    VMUFS_REQUEST_FORMAT,      /**< \brief Whole-card format. */
    VMUFS_REQUEST_DEFRAGMENT,  /**< \brief Safe file repacking. */
    VMUFS_REQUEST_RENAME,      /**< \brief Transactional file rename. */
    VMUFS_REQUEST_READ,        /**< \brief Bounded file-block read. */
    VMUFS_REQUEST_SET_ATTRIBUTES, /**< \brief Directory attribute update. */
    VMUFS_REQUEST_VOLUME_INFO, /**< \brief Volume inspection. */
    VMUFS_REQUEST_DELETE_FILES, /**< \brief Transactional multi-file deletion. */
    VMUFS_REQUEST_REWRITE,     /**< \brief Bounded file-block replacement. */
    VMUFS_REQUEST_REPAIR,      /**< \brief Orphan allocation reclamation. */
    VMUFS_REQUEST_BANK_INFO,   /**< \brief Memory-card bank query. */
    VMUFS_REQUEST_BANK_SELECT, /**< \brief Memory-card bank selection. */
    VMUFS_REQUEST_BANK_LOCK    /**< \brief Memory-card bank lock update. */
} vmufs_request_operation_t;

typedef enum vmufs_request_state {
    VMUFS_REQUEST_QUEUED,      /**< \brief Waiting for the worker. */
    VMUFS_REQUEST_RUNNING,     /**< \brief Storage operation active. */
    VMUFS_REQUEST_COMPLETE,    /**< \brief Successful terminal state. */
    VMUFS_REQUEST_CANCELLED,   /**< \brief Safely cancelled. */
    VMUFS_REQUEST_ERROR        /**< \brief Failed terminal state. */
} vmufs_request_state_t;

typedef enum vmufs_request_phase {
    VMUFS_REQUEST_PHASE_QUEUED,
    VMUFS_REQUEST_PHASE_PREPARING,
    VMUFS_REQUEST_PHASE_DATA,
    VMUFS_REQUEST_PHASE_FAT,
    VMUFS_REQUEST_PHASE_DIRECTORY,
    VMUFS_REQUEST_PHASE_CLEANUP,
    VMUFS_REQUEST_PHASE_ERASING,
    VMUFS_REQUEST_PHASE_FINISHED
} vmufs_request_phase_t;

typedef struct vmufs_request_status {
    vmufs_request_operation_t operation; /**< \brief Requested operation. */
    vmufs_request_state_t state;         /**< \brief Request lifecycle. */
    vmufs_request_phase_t phase;         /**< \brief Transaction phase. */
    int result;                          /**< \brief VMUFS result. */
    int error;                           /**< \brief errno value. */
    size_t completed_blocks;             /**< \brief Completed block work. */
    size_t total_blocks;                 /**< \brief Planned block work. */
    size_t data_blocks_completed;        /**< \brief Completed data blocks. */
    size_t data_blocks;                  /**< \brief Planned data blocks. */
    bool committed;                      /**< \brief Result fully published. */
} vmufs_request_status_t;

typedef void (*vmufs_request_callback_t)(
    vmufs_request_t *request, const vmufs_request_status_t *status, void *data);

/* Callbacks must return and must not destroy their own request. They may call
   ordinary VMU APIs, although a progress callback can block until the active
   storage transaction releases the VMU filesystem mutex. */

/** \brief Queue an exact file-relative block range read.

    The filename is copied during submission. The output buffer remains owned
    by the caller and must stay writable until terminal storage completion.
    On every terminal state, the first `data_blocks_completed * 512` bytes are
    complete and valid; bytes after that prefix have not been modified by the
    request. Cancellation takes effect between block reads.

    \param dev            VMU containing the file.
    \param fn             File name of at most 12 bytes.
    \param first_block    First file-relative block to read.
    \param outbuf         Destination for \p block_count contiguous blocks.
    \param block_count    Number of blocks to read; zero is permitted.
    \param callback       Optional progress and terminal callback.
    \param callback_data  Caller data passed to \p callback.
    \return               New request, or NULL with errno set.
*/
vmufs_request_t *vmufs_read_blocks_async(
    maple_device_t *dev, const char *fn, size_t first_block,
    void *outbuf, size_t block_count,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue an exact file-relative block range replacement.

    The filename is copied during submission. The source buffer remains owned
    by the caller and must stay readable until terminal storage completion.
    Cancellation and commit behavior match vmufs_rewrite_blocks().
*/
vmufs_request_t *vmufs_rewrite_blocks_async(
    maple_device_t *dev, const char *fn, size_t first_block,
    const void *inbuf, size_t block_count,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a complete volume-information query.

    The output remains owned by the caller and must stay writable until
    terminal storage completion. It is modified only when the request
    completes successfully. Cancellation is checked between metadata blocks.

    \param dev            Memory card to inspect.
    \param info           Destination for the complete volume summary.
    \param callback       Optional progress and terminal callback.
    \param callback_data  Caller data passed to \p callback.
    \return               New request, or NULL with errno set.
*/
vmufs_request_t *vmufs_get_volume_info_async(
    maple_device_t *dev, vmufs_volume_info_t *info,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a transactional file save.

    The filename is copied during submission. The input buffer remains owned
    by the caller and must stay readable until the request becomes terminal.
    New and replacement files use the same commit ordering as vmufs_write().
    Cancellation is accepted before the staging FAT is written; after that
    barrier the transaction completes the directory commit and any cleanup.

    \param dev            VMU to write.
    \param fn             File name of at most 12 bytes.
    \param inbuf          Input bytes, or NULL when \p insize is zero.
    \param insize         Input size in bytes.
    \param flags          VMUFS_OVERWRITE, VMUFS_VMUGAME, and VMUFS_NOCOPY.
    \param callback       Optional progress and terminal callback.
    \param callback_data  Caller data passed to \p callback.
    \return               New request, or NULL with errno set.
*/
vmufs_request_t *vmufs_write_async(
    maple_device_t *dev, const char *fn, const void *inbuf, size_t insize,
    int flags, vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a transactional file deletion.

    The filename is copied during submission. Cancellation is accepted before
    the directory removal is committed. FAT reclamation always follows a
    successful directory commit, even if cancellation is requested meanwhile.
*/
vmufs_request_t *vmufs_delete_async(
    maple_device_t *dev, const char *fn,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a transactional multi-file deletion.

    Every filename is validated and copied during submission. Once arguments
    have been accepted, the result is initialized even if a queued request is
    cancelled before running. The buffer remains caller-owned and must stay
    writable until terminal storage completion. Cancellation follows
    vmufs_delete_files()'s commit barrier.

    \param dev            VMU containing the files.
    \param filenames      Array of \p file_count names.
    \param file_count     Number of names; zero is permitted.
    \param result         Destination for the detailed outcome.
    \param callback       Optional progress and terminal callback.
    \param callback_data  Caller data passed to \p callback.
    \return               New request, or NULL with errno set.
*/
vmufs_request_t *vmufs_delete_files_async(
    maple_device_t *dev, const char *const *filenames, size_t file_count,
    vmufs_delete_result_t *result,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue validated orphan-only allocation repair.

    The result remains caller-owned and writable until terminal storage
    completion. Cancellation is accepted before the repaired FAT write.
*/
vmufs_request_t *vmufs_repair_async(
    maple_device_t *dev, vmufs_repair_result_t *result,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a memory-card bank information query. */
vmufs_request_t *vmufs_get_bank_info_async(
    maple_device_t *dev, vmu_memcard_bank_info_t *info,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a bank selection command serialized with VMUFS work. */
vmufs_request_t *vmufs_select_bank_async(
    maple_device_t *dev, uint8_t bank,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a bank-selection lock update serialized with VMUFS work. */
vmufs_request_t *vmufs_set_bank_locked_async(
    maple_device_t *dev, bool locked,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a transactional rename on one VMU.

    Both names are copied during submission. If the destination exists, its
    blocks are reclaimed only after the source is visible under the new name.
    Cancellation is accepted before the first directory commit.
*/
vmufs_request_t *vmufs_rename_async(
    maple_device_t *dev, const char *old_name, const char *new_name,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a safe directory-attribute update.

    The filename and attribute values are copied during submission.
    Cancellation takes effect before the directory block is committed.

    \param dev            VMU containing the file.
    \param fn             Existing file name of at most 12 bytes.
    \param attributes     New copy-protection and header-offset values.
    \param callback       Optional progress and terminal callback.
    \param callback_data  Caller data passed to \p callback.
    \return               New request, or NULL with errno set.
*/
vmufs_request_t *vmufs_set_file_attributes_async(
    maple_device_t *dev, const char *fn,
    const vmufs_file_attributes_t *attributes,
    vmufs_request_callback_t callback, void *callback_data);

/** \brief Queue a destructive standard-card format.

    Options are copied during submission. Cancellation is accepted while the
    request is queued and before the invalid-root write.
*/
vmufs_request_t *vmufs_format_async(
    maple_device_t *dev, const vmufs_format_options_t *options,
    vmufs_format_mode_t mode, vmufs_request_callback_t callback,
    void *callback_data);

/** \brief Queue interruption-safe whole-card defragmentation.

    Cancellation takes effect between copy-on-write commit barriers. A file
    move whose staging FAT has been published is completed before cancellation
    can become terminal.
*/
vmufs_request_t *vmufs_defragment_async(
    maple_device_t *dev, vmufs_request_callback_t callback,
    void *callback_data);

int vmufs_request_get_status(const vmufs_request_t *request,
                             vmufs_request_status_t *status);
int vmufs_request_cancel(vmufs_request_t *request);

/** \brief Wait for terminal storage completion; zero timeout waits forever. */
int vmufs_request_wait(vmufs_request_t *request, uint32_t timeout,
                       vmufs_request_status_t *status);

/** \brief Wait for terminal completion and callback delivery.

    Calling this from the VMU callback dispatcher fails with `EDEADLK`.
*/
int vmufs_request_wait_callback(vmufs_request_t *request, uint32_t timeout);

/** \brief Destroy a terminal request after its callback has returned. */
int vmufs_request_destroy(vmufs_request_t *request);

/** @} */

/** \brief  Return the number of user blocks free for file writing.

    You should check this number before attempting to write.

    \return                 The number of blocks free for writing.
*/
int vmufs_free_blocks(maple_device_t *dev);

/** \brief Return contiguous space eligible for an executable image.

    A VMU executable must begin at physical block zero. This count therefore
    stops at the first allocated block and can be much smaller than the total
    reported by vmufs_free_blocks() on a fragmented card.

    \param dev  VMU to inspect.
    \return     Contiguous free blocks beginning at block zero, or -1 on I/O
                or geometry failure.
*/
int vmufs_free_executable_blocks(maple_device_t *dev);


/** \brief  Initialize vmufs.

    Must be called before anything else is useful.

    \retval 0               On success (no error conditions defined).
*/
int vmufs_init(void);

/** \brief  Shutdown vmufs.

    Must be called after everything is finished.
*/
int vmufs_shutdown(void);

/** @} */

__END_DECLS

#endif  /* __DC_VMUFS_H */
