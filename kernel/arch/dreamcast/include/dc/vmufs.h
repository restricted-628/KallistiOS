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
    VMUFS_REQUEST_DEFRAGMENT   /**< \brief Safe file repacking. */
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
    size_t completed_blocks;             /**< \brief Completed writes. */
    size_t total_blocks;                 /**< \brief Planned writes. */
    size_t data_blocks_completed;        /**< \brief Completed data writes. */
    size_t data_blocks;                  /**< \brief Planned data writes. */
    bool committed;                      /**< \brief Visible result committed. */
} vmufs_request_status_t;

typedef void (*vmufs_request_callback_t)(
    vmufs_request_t *request, const vmufs_request_status_t *status, void *data);

/* Callbacks must return and must not destroy their own request. They may call
   ordinary VMU APIs, although a progress callback can block until the active
   storage transaction releases the VMU filesystem mutex. */

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
