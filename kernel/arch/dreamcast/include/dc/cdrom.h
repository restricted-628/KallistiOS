/* KallistiOS ##version##

   dc/cdrom.h
   Copyright (C) 2000-2001 Megan Potter
   Copyright (C) 2014, 2025 Donald Haase
   Copyright (C) 2023, 2024, 2025 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black
*/

#ifndef __DC_CDROM_H
#define __DC_CDROM_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <dc/syscalls.h>
#include <kos/regfield.h>

/** \file    dc/cdrom.h
    \brief   CD access to the GD-ROM drive.
    \ingroup gdrom

    This file contains the interface to the Dreamcast's GD-ROM drive. It is
    simply called cdrom.h and cdrom.c because, by design, you cannot directly
    use this code to read the high-density area of GD-ROMs. This is the way it
    always has been, and always will be.

    The way things are set up, as long as you're using fs_iso9660 to access the
    CD, it will automatically detect and react to disc changes for you.

    This file only facilitates reading raw sectors and doing other fairly low-
    level things with CDs. If you're looking for higher-level stuff, like
    normal file reading, consult with the stuff for the fs and for fs_iso9660.

    If you're looking for *even lower* level things with CDs, see the gdrom
    related syscalls or g1ata.

    \author Megan Potter
    \author Donald Haase
    \author Ruslan Rostovtsev

    \see    kos/fs.h
    \see    dc/fs_iso9660.h
    \see    dc/syscalls.h
    \see    dc/g1ata.h
*/

/** \defgroup gdrom     GD-ROM
    \brief              Driver for the Dreamcast's GD-ROM drive
    \ingroup            vfs
*/

/* These are defines provided for compatibility. These defines are now part of `cd_cmd_code_t` in dc/syscalls.h */
static const uint8_t  CMD_CHECK_LICENSE      __depr("Please use the new CD_ prefixed versions.") = CD_CMD_CHECK_LICENSE;
static const uint8_t  CMD_REQ_SPI_CMD        __depr("Please use the new CD_ prefixed versions.") = CD_CMD_REQ_SPI_CMD;
static const uint8_t  CMD_PIOREAD            __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PIOREAD;
static const uint8_t  CMD_DMAREAD            __depr("Please use the new CD_ prefixed versions.") = CD_CMD_DMAREAD;
static const uint8_t  CMD_GETTOC             __depr("Please use the new CD_ prefixed versions.") = CD_CMD_GETTOC;
static const uint8_t  CMD_GETTOC2            __depr("Please use the new CD_ prefixed versions.") = CD_CMD_GETTOC2;
static const uint8_t  CMD_PLAY               __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PLAY_TRACKS;
static const uint8_t  CMD_PLAY2              __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PLAY_SECTORS;
static const uint8_t  CMD_PAUSE              __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PAUSE;
static const uint8_t  CMD_RELEASE            __depr("Please use the new CD_ prefixed versions.") = CD_CMD_RELEASE;
static const uint8_t  CMD_INIT               __depr("Please use the new CD_ prefixed versions.") = CD_CMD_INIT;
static const uint8_t  CMD_DMA_ABORT          __depr("Please use the new CD_ prefixed versions.") = CD_CMD_DMA_ABORT;
static const uint8_t  CMD_OPEN_TRAY          __depr("Please use the new CD_ prefixed versions.") = CD_CMD_OPEN_TRAY;
static const uint8_t  CMD_SEEK               __depr("Please use the new CD_ prefixed versions.") = CD_CMD_SEEK;
static const uint8_t  CMD_DMAREAD_STREAM     __depr("Please use the new CD_ prefixed versions.") = CD_CMD_DMAREAD_STREAM;
static const uint8_t  CMD_NOP                __depr("Please use the new CD_ prefixed versions.") = CD_CMD_NOP;
static const uint8_t  CMD_REQ_MODE           __depr("Please use the new CD_ prefixed versions.") = CD_CMD_REQ_MODE;
static const uint8_t  CMD_SET_MODE           __depr("Please use the new CD_ prefixed versions.") = CD_CMD_SET_MODE;
static const uint8_t  CMD_SCAN_CD            __depr("Please use the new CD_ prefixed versions.") = CD_CMD_SCAN_CD;
static const uint8_t  CMD_STOP               __depr("Please use the new CD_ prefixed versions.") = CD_CMD_STOP;
static const uint8_t  CMD_GETSCD             __depr("Please use the new CD_ prefixed versions.") = CD_CMD_GETSCD;
static const uint8_t  CMD_GETSES             __depr("Please use the new CD_ prefixed versions.") = CD_CMD_GETSES;
static const uint8_t  CMD_REQ_STAT           __depr("Please use the new CD_ prefixed versions.") = CD_CMD_REQ_STAT;
static const uint8_t  CMD_PIOREAD_STREAM     __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PIOREAD_STREAM;
static const uint8_t  CMD_DMAREAD_STREAM_EX  __depr("Please use the new CD_ prefixed versions.") = CD_CMD_DMAREAD_STREAM_EX;
static const uint8_t  CMD_PIOREAD_STREAM_EX  __depr("Please use the new CD_ prefixed versions.") = CD_CMD_PIOREAD_STREAM_EX;
static const uint8_t  CMD_GET_VERS           __depr("Please use the new CD_ prefixed versions.") = CD_CMD_GET_VERS;
static const uint8_t  CMD_MAX                __depr("Please use the new CD_ prefixed versions.") = CD_CMD_MAX;

/** \defgroup cd_cmd_response       Command Responses
    \brief                          Responses from GD-ROM syscalls
    \ingroup  gdrom

    These are the values that the various functions can return as error codes.
    @{
*/
#define ERR_OK          0   /**< \brief No error */
#define ERR_NO_DISC     1   /**< \brief No disc in drive */
#define ERR_DISC_CHG    2   /**< \brief Disc changed, but not reinitted yet */
#define ERR_SYS         3   /**< \brief System error */
#define ERR_ABORTED     4   /**< \brief Command aborted */
#define ERR_NO_ACTIVE   5   /**< \brief System inactive? */
#define ERR_TIMEOUT     6   /**< \brief Aborted due to timeout */
#define ERR_RECOVERED   7   /**< \brief Drive reported a recovered error */
#define ERR_NOT_READY   8   /**< \brief Drive is temporarily not ready */
#define ERR_MEDIA       9   /**< \brief Medium error */
#define ERR_HARDWARE   10   /**< \brief Drive hardware error */
#define ERR_ILLEGAL_REQUEST 11 /**< \brief Command or parameter was rejected */
#define ERR_PROTECT    12   /**< \brief Operation is prohibited/protected */
#define ERR_NOT_READABLE 13 /**< \brief Inserted medium cannot be read */
#define ERR_BUSY       14   /**< \brief Command server or G1 path is busy */
/** @} */

/** \brief GD-ROM command sense keys returned in `cd_cmd_chk_status_t.err1`.

    These values are the GD-ROM BIOS result categories returned by the
    Dreamcast firmware. They resemble SCSI sense keys, but include Dreamcast-
    specific values and should not be treated as an unrestricted SCSI status.
*/
typedef enum cdrom_sense_key {
    CDROM_SENSE_NONE            = 0x00,
    CDROM_SENSE_RECOVERED_ERROR = 0x01,
    CDROM_SENSE_NOT_READY       = 0x02,
    CDROM_SENSE_MEDIUM_ERROR    = 0x03,
    CDROM_SENSE_HARDWARE_ERROR  = 0x04,
    CDROM_SENSE_ILLEGAL_REQUEST = 0x05,
    CDROM_SENSE_UNIT_ATTENTION  = 0x06,
    CDROM_SENSE_DATA_PROTECT    = 0x07,
    CDROM_SENSE_ABORTED_COMMAND = 0x0b,
    CDROM_SENSE_NOT_READABLE    = 0x10,
    CDROM_SENSE_G1_SEMAPHORE    = 0x20
} cdrom_sense_key_t;

/** \brief Decoded GD-ROM command sense information. */
typedef struct cdrom_sense {
    cdrom_sense_key_t key; /**< \brief General error category from `err1`. */
    uint8_t asc;           /**< \brief Additional sense code. */
    uint8_t ascq;          /**< \brief Additional sense code qualifier. */
} cdrom_sense_t;

/** \brief Decode the raw status words returned by the GD-ROM BIOS.

    The low byte of `err2` is the additional sense code and its next byte is
    the qualifier. The original signed fields remain available to callers who
    need the complete firmware result.

    \retval 0              Status decoded successfully.
    \retval -1             A pointer was NULL, with errno set to `EINVAL`.
*/
int cdrom_decode_sense(const cd_cmd_chk_status_t *detail,
                       cdrom_sense_t *sense);

/** \brief Map decoded GD-ROM sense information to a KOS `ERR_*` result.

    This mapping is shared by BIOS-backed commands and direct SPI commands.
    A not-ready response with ASC 0x3a maps to the more specific
    `ERR_NO_DISC` result.

    \param  sense           Decoded drive sense information.
    \return                 A stable KOS GD-ROM result code. A NULL pointer
                            maps to `ERR_SYS` and sets errno to `EINVAL`.
*/
int cdrom_sense_to_result(const cdrom_sense_t *sense);

/** \brief Map a BIOS response and raw status to a KOS `ERR_*` result.

    Completed and streaming responses map to `ERR_OK`; in-progress responses
    map to `ERR_BUSY`. Failed commands are classified by their sense key. A
    not-ready response with ASC 0x3a is the more specific `ERR_NO_DISC`.

    \param  response        Response returned by the BIOS command server.
    \param  detail          Raw BIOS status, required for a failed response.

    \return                 A stable KOS GD-ROM result code.
*/
int cdrom_status_to_result(cd_cmd_chk_t response,
                           const cd_cmd_chk_status_t *detail);

/** \brief Map a KOS GD-ROM `ERR_*` result to a portable errno value.

    `ERR_NO_DISC` maps to `ENODEV`, since KOS's Newlib configuration does not
    expose the non-portable `ENOMEDIUM`. Callers needing that distinction can
    inspect the result and decoded sense directly.

    \return                 Zero for `ERR_OK`, otherwise an errno value.
*/
int cdrom_result_to_errno(int result);

/** \brief Dreamcast disc identity parsed from the IP system area.

    The product identifier contains at most ten characters and is always NUL
    terminated by KOS. The larger array leaves room for future request metadata
    without exposing a firmware-specific structure as ABI.
*/
typedef struct cdrom_disc_id {
    uint32_t disc_number;     /**< \brief One-based disc number in the set. */
    uint32_t disc_count;      /**< \brief Total number of discs in the set. */
    char product_id[16];      /**< \brief NUL-terminated product identifier. */
} cdrom_disc_id_t;

/** \brief Run the BootROM's bounded Dreamcast-disc recognition process.

    This is the KOS counterpart of the media-recognition step required
    after the drive reports unit attention. It owns the shared G1 path for the
    complete sequence, performs the BootROM-required cache purge before every
    attempt, and calls the system service no more than once every 20 ms.

    The operation uses a BootROM system service. It is not part of the direct
    direct SPI transport and does not change the selected ISO9660 backend.

    \param  timeout          Required nonzero timeout in milliseconds.
    \retval 0               A Dreamcast-compatible disc was recognized.
    \retval 1               The inserted medium is not Dreamcast-compatible.
    \retval -1              Invalid arguments or timeout, with errno set.
*/
int cdrom_media_recognize(uint32_t timeout);

/** \brief Read the identity of the disc which booted the application.

    \param  id               Output for the parsed disc number/count/product.
    \retval 0               Identity parsed successfully.
    \retval -1              Invalid pointer or malformed system area.
*/
int cdrom_get_boot_disc_id(cdrom_disc_id_t *id);

/** \brief Read the identity populated by the latest media recognition.

    This record is meaningful only after cdrom_media_recognize() returned 0
    and the drive door has not subsequently been opened.

    \param  id               Output for the parsed disc number/count/product.
    \retval 0               Identity parsed successfully.
    \retval -1              Invalid pointer or malformed system area.
*/
int cdrom_get_current_disc_id(cdrom_disc_id_t *id);

/* These are defines provided for compatibility. These defines are now part of `cd_cmd_chk_t` in dc/syscalls.h */
static const uint8_t  FAILED      __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_FAILED;
static const uint8_t  NO_ACTIVE   __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_NOT_FOUND;
static const uint8_t  PROCESSING  __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_PROCESSING;
static const uint8_t  COMPLETED   __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_COMPLETED;
static const uint8_t  STREAMING   __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_STREAMING;
static const uint8_t  BUSY        __depr("Please use the new CD_CMD_ prefixed versions.") = CD_CMD_BUSY;

/** \defgroup cdda_read_modes       CDDA Read Modes
    \brief                          Read modes for CDDA
    \ingroup  gdrom

    Valid values to pass to the cdrom_cdda_play() function for the mode
    parameter.
    @{
*/
#define CDDA_TRACKS     1   /**< \brief Play by track number */
#define CDDA_SECTORS    2   /**< \brief Play by sector number */
/** @} */

/* Compat. These got converted to a plain bool. */
static const bool  CDROM_READ_PIO   __depr("Please just use false to not use dma.") = false;
static const bool  CDROM_READ_DMA   __depr("Please just use true to use dma.") = true;

/* Compat. This can now be found in dc/syscalls.h */
#define CDROM_TOC __depr("Use the type cd_toc_t rather than CDROM_TOC.") cd_toc_t

/** \defgroup cd_toc_access         TOC Access Macros
    \brief                          Macros used to access the TOC
    \ingroup  gdrom

    @{
*/

/** \brief  Get the FAD address of a TOC entry.
    \param  n               The actual entry from the TOC to look at.
    \return                 The FAD of the entry.
*/
#define TOC_LBA(n) FIELD_GET(n, 0x00ffffff)

/** \brief  Get the address of a TOC entry.
    \param  n               The entry from the TOC to look at.
    \return                 The entry's address.
*/
#define TOC_ADR(n) FIELD_GET(n, 0x0f000000)

/** \brief  Get the control data of a TOC entry.
    \param  n               The entry from the TOC to look at.
    \return                 The entry's control value.
*/
#define TOC_CTRL(n) FIELD_GET(n, 0xf0000000)

/** \brief  Get the track number of a TOC entry.
    \param  n               The entry from the TOC to look at.
    \return                 The entry's track.
*/
#define TOC_TRACK(n) FIELD_GET(n, 0x00ff0000)
/** @} */

/** \brief  CD-ROM streams callback
*/
typedef void (*cdrom_stream_callback_t)(void *data);

/** \defgroup cdrom_requests Asynchronous GD-ROM requests
    \brief Asynchronous GD-ROM command requests.
    \ingroup gdrom

    These requests serialize access to the physical GD-ROM command path. BIOS
    requests preserve the complete command-server result; direct requests
    identify their backend explicitly and publish the same lifecycle and byte
    accounting. Completion callbacks run in normal thread context, never from
    a GD DMA interrupt. The request and callback workers are created atomically
    on the first valid submission; synchronous-only applications reserve no
    asynchronous worker stacks. First use can fail with `ENOMEM`.

    @{ */

/** \brief Opaque asynchronous GD-ROM request. */
typedef struct cdrom_request cdrom_request_t;

/** \brief State of an asynchronous GD-ROM request. */
typedef enum cdrom_request_state {
    CDROM_REQUEST_QUEUED,       /**< \brief Waiting for ownership of the drive. */
    CDROM_REQUEST_RUNNING,      /**< \brief Being dispatched or run by the BIOS. */
    CDROM_REQUEST_COMPLETE,     /**< \brief Completed successfully. */
    CDROM_REQUEST_CANCELLED,    /**< \brief Cancelled by the caller or shutdown. */
    CDROM_REQUEST_ERROR,        /**< \brief Completed with a drive/command error. */
    CDROM_REQUEST_TIMED_OUT     /**< \brief Timed out and was aborted. */
} cdrom_request_state_t;

/** \brief Transport backend servicing an asynchronous request. */
typedef enum cdrom_request_backend {
    CDROM_REQUEST_BACKEND_BIOS,   /**< \brief Dreamcast BIOS command server. */
    CDROM_REQUEST_BACKEND_DIRECT  /**< \brief Direct GD-ROM transport. */
} cdrom_request_backend_t;

/** \brief Status and detailed result of an asynchronous GD-ROM request.

    For data requests, the three byte totals distinguish the caller's logical
    request, useful payload available before EOF, and sector-rounded physical
    I/O. `completed_bytes` tracks useful payload progress while
    `io_completed_bytes` tracks the underlying command. Consequently,
    `remaining_bytes` can remain nonzero after a successful request that
    reached EOF. Once a request is terminal, the first `completed_bytes` of
    caller-visible payload are coherent and valid, including after
    cancellation, timeout, or error; later bytes have no defined contents.
*/
typedef struct cdrom_request_status {
    cd_cmd_code_t command;          /**< \brief Logical GD-ROM command. */
    cdrom_request_backend_t backend; /**< \brief Transport servicing it. */
    cdrom_request_state_t state;    /**< \brief Current request state. */
    int result;                     /**< \brief Mapped KOS `ERR_*` result. */
    int error;                      /**< \brief Mapped errno value, or zero. */
    cd_cmd_chk_t response;          /**< \brief Raw BIOS response, if used. */
    cd_cmd_chk_status_t detail;     /**< \brief BIOS detail/progress, if used. */
    cdrom_sense_t sense;            /**< \brief Decoded command sense. */
    size_t requested_bytes;     /**< \brief Payload bytes requested by the caller. */
    size_t data_bytes;          /**< \brief Payload bytes available to return. */
    size_t completed_bytes;     /**< \brief Payload bytes completed so far. */
    size_t remaining_bytes;     /**< \brief Requested payload bytes not returned. */
    size_t io_bytes;            /**< \brief Bytes scheduled for physical transfer. */
    size_t io_completed_bytes;  /**< \brief Physical bytes transferred so far. */
} cdrom_request_status_t;

/** \brief Completion callback for an asynchronous GD-ROM request.

    The callback is dispatched by a dedicated KOS thread after the terminal
    status and completed data are published. It cannot delay the GD-ROM request
    queue. The status pointer is valid only for the duration of the callback.

    A callback may call cdrom_request_wait() for a different request, but must
    not wait for its own request or call cdrom_request_wait_callback() from the
    callback dispatcher. A request cannot be destroyed from its callback.
*/
typedef void (*cdrom_request_callback_t)(cdrom_request_t *request,
                                         const cdrom_request_status_t *status,
                                         void *data);

/** \brief Submit an asynchronous GD-ROM control command.

    The parameter block is copied before this function returns. Any buffers
    referenced by pointers inside that block must remain valid until the
    request completes. Requests are queued and executed one at a time through
    KOS's normal G1/GD-ROM ownership mechanism.

    Raw read, DMA read, DMA abort, and streaming commands are intentionally
    rejected until their transfer ownership is integrated with this request
    engine. Use the existing blocking or streaming APIs for those operations.

    \param command      BIOS GD-ROM command to submit.
    \param params       Optional command parameter block.
    \param params_size  Size of the parameter block in bytes.
    \param timeout      Timeout in milliseconds after the BIOS command starts,
                        or zero for no timeout. Time spent queued for drive
                        ownership is not included.
    \param callback     Optional completion callback.
    \param callback_data User data passed to the callback.

    \return             A new request on success, or NULL with errno set.
*/
cdrom_request_t *cdrom_request_submit(cd_cmd_code_t command,
                                      const void *params, size_t params_size,
                                      uint32_t timeout,
                                      cdrom_request_callback_t callback,
                                      void *callback_data);

/** \brief Copy the current status of an asynchronous request.

    BIOS DMA progress is refreshed by the vblank monitor and therefore has a
    granularity of up to one video frame. Direct DMA progress is sampled by its
    sleeping request owner at a bounded 16 ms cadence. This function never
    re-enters the GD-ROM BIOS command server or touches the drive task file.

    \retval 0           On success.
    \retval -1          On invalid arguments, with errno set.
*/
int cdrom_request_get_status(const cdrom_request_t *request,
                             cdrom_request_status_t *status);

/** \brief Wait for a request to enter a terminal state.

    A successful return means the command and all driver-level finalization
    finished, not necessarily that the command succeeded. An optional user
    callback may still be pending or running. Inspect `status->state`,
    `status->error`, and the raw BIOS result fields for the outcome. Use
    cdrom_request_wait_callback() before destroying a request with a callback.

    \param request      Request to wait for.
    \param timeout      Wait timeout in milliseconds, or zero to wait forever.
    \param status       Optional output for the final status.

    \retval 0           The request reached a terminal state.
    \retval -1          The wait timed out or an argument was invalid.
*/
int cdrom_request_wait(cdrom_request_t *request, uint32_t timeout,
                       cdrom_request_status_t *status);

/** \brief Wait for terminal completion and callback dispatch.

    This is only needed when the caller must know that its optional callback
    returned, normally before destroying the request. It must not be called
    from a GD-ROM request callback.

    \param request      Request to wait for.
    \param timeout      Wait timeout in milliseconds, or zero to wait forever.

    \retval 0           The request and callback finished.
    \retval -1          Timed out, invalid request, or callback-context call.
*/
int cdrom_request_wait_callback(cdrom_request_t *request, uint32_t timeout);

/** \brief Cancel a queued or running request.

    Queued requests complete as cancelled without accessing the drive. A
    running request is asked to abort and completes asynchronously.

    \retval 0           Cancellation was accepted or the request was finished.
    \retval -1          The request was invalid.
*/
int cdrom_request_cancel(cdrom_request_t *request);

/** \brief Destroy a completed request.

    \retval 0           The request was destroyed.
    \retval -1          The request is active or its callback is pending/running.
*/
int cdrom_request_destroy(cdrom_request_t *request);

/** @} */

/** \defgroup cdrom_stream_sessions Staged GD-ROM stream sessions
    \brief Drive read-ahead with application-directed RAM transfers.
    \ingroup gdrom

    A stream session queues behind ordinary GD-ROM requests, then owns
    the drive until all staged bytes are transferred or the session is
    cancelled. BIOS and direct SPI sessions share this lifecycle; inspect
    `status.backend` to identify the selected transport. RAM transfers are
    represented by normal \ref cdrom_request_t objects, so their progress,
    cancellation, waiting, and callbacks use the same rules as other
    asynchronous DMA.

    @{ */

/** \brief Opaque staged GD-ROM stream session. */
typedef struct cdrom_stream_session cdrom_stream_session_t;

/** \brief State of a staged GD-ROM stream session. */
typedef enum cdrom_stream_session_state {
    CDROM_STREAM_SESSION_QUEUED,
    CDROM_STREAM_SESSION_STARTING,
    CDROM_STREAM_SESSION_READY,
    CDROM_STREAM_SESSION_COMPLETE,
    CDROM_STREAM_SESSION_CANCELLED,
    CDROM_STREAM_SESSION_ERROR,
    CDROM_STREAM_SESSION_TIMED_OUT
} cdrom_stream_session_state_t;

/** \brief Status of a staged GD-ROM stream session.

    `state` describes drive read-ahead ownership. An active RAM transfer has
    its own \ref cdrom_request_status_t, deliberately keeping the two state
    machines separate.
*/
typedef struct cdrom_stream_session_status {
    cdrom_request_backend_t backend;      /**< \brief Transport owning G1. */
    cdrom_stream_session_state_t state; /**< \brief Drive-session state. */
    int result;                         /**< \brief Mapped KOS result. */
    int error;                          /**< \brief Mapped errno value. */
    cd_cmd_chk_t response;              /**< \brief Raw BIOS response. */
    cd_cmd_chk_status_t detail;         /**< \brief Raw BIOS status. */
    cdrom_sense_t sense;                /**< \brief Decoded command sense. */
    uint32_t start_sector;              /**< \brief First disc FAD. */
    size_t sector_count;                /**< \brief Physical sector count. */
    size_t total_bytes;                 /**< \brief Physical staged bytes. */
    size_t data_bytes;                  /**< \brief Useful payload bytes. */
    size_t transferred_bytes;           /**< \brief Completed physical bytes. */
    size_t completed_bytes;             /**< \brief Completed payload bytes. */
    size_t remaining_bytes;             /**< \brief Physical bytes remaining. */
    bool transfer_active;               /**< \brief A RAM request is active. */
    uint32_t idle_timeout;               /**< \brief Ready-idle limit in ms. */
} cdrom_stream_session_status_t;

/** \brief Queue a BIOS DMA staged-read session for a raw FAD range.

    This constructor preserves the BIOS-backed CD-ROM API. Use
    \ref gdrom_direct_stream_session_start for an explicit direct SPI session;
    both return the common session type below.

    `start_timeout` covers drive ownership acquisition and reaching the BIOS
    streaming state. Once ready, the session intentionally retains ownership
    until its complete range is transferred, it is cancelled, or no transfer
    is submitted within `idle_timeout`. The idle timer restarts after every
    completed or queued-cancelled transfer. Expiry aborts the stream, releases
    G1 ownership, and completes the session as timed out.

    \param sector          First disc sector as a FAD.
    \param sector_count    Number of sectors to stage.
    \param start_timeout   Startup timeout in milliseconds, or zero for none.
    \param idle_timeout    Required nonzero ready-idle timeout in milliseconds.

    \return                A queued session, or NULL with errno set.
*/
cdrom_stream_session_t *cdrom_stream_session_start(
    uint32_t sector, size_t sector_count, uint32_t start_timeout,
    uint32_t idle_timeout);

/** \brief Copy the current staged-stream status. */
int cdrom_stream_session_get_status(
    const cdrom_stream_session_t *session,
    cdrom_stream_session_status_t *status);

/** \brief Wait until a staged stream is ready or terminal.

    A successful return reports either `CDROM_STREAM_SESSION_READY` or a
    terminal state; inspect the returned status for the outcome.
*/
int cdrom_stream_session_wait_ready(
    cdrom_stream_session_t *session, uint32_t timeout,
    cdrom_stream_session_status_t *status);

/** \brief Wait for a staged stream to enter a terminal state. */
int cdrom_stream_session_wait(
    cdrom_stream_session_t *session, uint32_t timeout,
    cdrom_stream_session_status_t *status);

/** \brief Transfer staged bytes to RAM asynchronously using GD DMA.

    Only one transfer may be active per session. `buffer` and `bytes` must be
    32-byte aligned, and the transfer cannot exceed the session's remaining
    range. The returned request owns its normal callback and destruction
    lifecycle independently of the session. Cancelling or timing out a transfer
    after DMA starts aborts the underlying BIOS stream, so the parent session
    becomes terminal too. Cancelling a transfer while it is still queued leaves
    the ready drive session usable.

    A nonzero `timeout` bounds the active RAM transfer in milliseconds. Zero
    preserves the legacy unbounded behavior for a BIOS session; a direct SPI
    session substitutes a bounded 30-second safety timeout so a lost DMARQ
    cannot retain the shared G1 path forever.
*/
cdrom_request_t *cdrom_stream_session_transfer_async(
    cdrom_stream_session_t *session, void *buffer, size_t bytes,
    uint32_t timeout, cdrom_request_callback_t callback,
    void *callback_data);

/** \brief Cancel a queued or active staged stream and its active transfer. */
int cdrom_stream_session_cancel(cdrom_stream_session_t *session);

/** \brief Destroy a terminal staged stream with no active transfer request. */
int cdrom_stream_session_destroy(cdrom_stream_session_t *session);

/** @} */

/** \defgroup cdrom_sector_ranges Bounded raw-disc sector ranges
    \brief Seekable FAD windows over the BIOS or direct transport.
    \ingroup gdrom

    A sector range is a KOS-owned, seekable view of a fixed sequence of cooked
    2,048-byte disc sectors. It provides the useful behavior of opening a raw
    disc range without importing a filesystem handle, caller work area, or
    global current operation. One read, preseek, or stream may own a range at a
    time; unrelated ranges still share the normal request queue.

    @{ */

/** \brief Opaque bounded raw-disc sector range. */
typedef struct cdrom_sector_range cdrom_sector_range_t;

/** \brief Immutable geometry and current cursor of a sector range. */
typedef struct cdrom_sector_range_info {
    cdrom_request_backend_t backend; /**< \brief BIOS or direct transport. */
    uint32_t start_fad;              /**< \brief First absolute frame address. */
    size_t sector_count;             /**< \brief Total sectors in the range. */
    size_t position;                 /**< \brief Current sector offset. */
} cdrom_sector_range_info_t;

/** \brief Open a bounded 2,048-byte-sector range on the BIOS transport.

    This operation allocates only KOS bookkeeping and does not access the
    drive. The range is inclusive of `start_fad` and contains exactly
    `sector_count` sectors.

    \param start_fad       First absolute frame address, at least 150.
    \param sector_count    Required nonzero number of sectors.
    \return                A new range, or `NULL` with errno set.
*/
cdrom_sector_range_t *cdrom_sector_range_open(
    uint32_t start_fad, size_t sector_count);

/** \brief Close an idle sector range.

    \retval 0              The range was closed.
    \retval -1             An operation still owns the range (`EBUSY`).
*/
int cdrom_sector_range_close(cdrom_sector_range_t *range);

/** \brief Copy a range's backend, geometry, and current cursor. */
int cdrom_sector_range_get_info(
    cdrom_sector_range_t *range, cdrom_sector_range_info_t *info);

/** \brief Seek the range cursor in units of 2,048-byte sectors.

    Positions beyond the range are clamped to its end. Seeking before its
    start fails. An active read, preseek, or stream makes this return `EBUSY`.

    \return                New sector offset, or -1 with errno set.
*/
int64_t cdrom_sector_range_seek(
    cdrom_sector_range_t *range, int64_t offset, int whence);

/** \brief Return the current sector offset, or -1 with errno set. */
int64_t cdrom_sector_range_tell(cdrom_sector_range_t *range);

/** \brief Report whether the cursor is at the end of the range. */
int cdrom_sector_range_eof(cdrom_sector_range_t *range, bool *eof);

/** \brief Read cooked sectors synchronously from the current cursor.

    The request is clipped at the range end and advances the cursor by the
    number of sectors returned. The destination may be unaligned; KOS uses a
    bounded aligned workspace when necessary. Physical commands are limited
    to 16 sectors and yield G1 between chunks. `timeout` is a required nonzero
    deadline for a nonempty logical read; a zero-sector read does no I/O.

    \return                Number of sectors read, zero at EOF, or -1.
*/
ssize_t cdrom_sector_range_read(
    cdrom_sector_range_t *range, void *buffer, size_t sector_count,
    uint32_t timeout);

/** \brief Read cooked sectors asynchronously from the current cursor.

    The destination must be 32-byte aligned and remain untouched until the
    returned request is terminal. Large reads use bounded 16-sector DMA
    commands and requeue between chunks. A successful request advances the
    cursor atomically; cancellation, timeout, or error leaves it unchanged,
    while the request status still identifies any valid completed prefix.
    A nonempty read requires a nonzero timeout.

    \return                A request, or `NULL` with errno set.
*/
cdrom_request_t *cdrom_sector_range_read_async(
    cdrom_sector_range_t *range, void *buffer, size_t sector_count,
    uint32_t timeout, cdrom_request_callback_t callback,
    void *callback_data);

/** \brief Queue a pickup preseek for the range's current cursor. */
cdrom_request_t *cdrom_sector_range_preseek_async(
    cdrom_sector_range_t *range, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief Start staged read-ahead at the range's current cursor.

    The requested count is clipped to the range end. Completed staged payload
    advances the cursor even if the session later terminates through cancel,
    timeout, or error, matching ordinary ISO staged-stream accounting.
*/
cdrom_stream_session_t *cdrom_sector_range_stream_start(
    cdrom_sector_range_t *range, size_t sector_count,
    uint32_t start_timeout, uint32_t idle_timeout);

/** @} */

/** \brief    Set the sector size for read sectors.
    \ingroup  gdrom

    This function sets the sector size that the cdrom_read_sectors() function
    will return. Be sure to set this to the correct value for the type of
    sectors you're trying to read. Common values are 2048 (for reading CD-ROM
    sectors) or 2352 (for reading raw sectors).

    \param  size            The size of the sector data.

    \return                 \ref cd_cmd_response
*/
int cdrom_set_sector_size(int size);

/** \brief    Execute a CD-ROM command.
    \ingroup  gdrom

    This function executes the specified command using the BIOS syscall for
    executing GD-ROM commands.

    \param  cmd             The command to execute.
    \param  param           Data to pass to the syscall.

    \return                 \ref cd_cmd_response
*/
int cdrom_exec_cmd(cd_cmd_code_t cmd, void *param);

/** \brief    Execute a CD-ROM command with timeout.
    \ingroup  gdrom

    This function executes the specified command using the BIOS syscall for
    executing GD-ROM commands with timeout.

    \param  cmd             The command to execute.
    \param  param           Data to pass to the syscall.
    \param  timeout         Timeout in milliseconds.

    \return                 \ref cd_cmd_response
*/
int cdrom_exec_cmd_timed(cd_cmd_code_t cmd, void *param, uint32_t timeout);

/** \brief    Abort a CD-ROM command with timeout.
    \ingroup  gdrom

    This function aborts current command using the BIOS syscall for
    aborting GD-ROM commands. They can also abort DMA transfers.

    \param  timeout         Timeout in milliseconds.
    \param  abort_dma       Whether to abort the DMA transfer.

    \return                 \ref cd_cmd_response
*/
int cdrom_abort_cmd(uint32_t timeout, bool abort_dma);

/** \brief    Get the status of the GD-ROM drive.
    \ingroup  gdrom

    \param  status          Space to return the drive's status.
    \param  disc_type       Space to return the type of disc in the drive.

    \return                 \ref cd_cmd_response
    \see    cd_status_values
    \see    cd_disc_types
*/
int cdrom_get_status(int *status, int *disc_type);

/** \brief Cached GD-ROM drive state.
    \ingroup gdrom

    The media monitor updates this snapshot without requiring the caller to
    wait for G1/GD-ROM ownership. Observations include successful status
    samples, failed samples, and media-significant request failures.
    `sequence` advances for every published observation, while `timestamp`
    records when it was published.
*/
typedef struct cdrom_drive_state {
    cd_stat_t status;              /**< \brief Current drive status. */
    cd_disc_types_t disc_type;     /**< \brief Current disc type. */
    cdrom_request_backend_t backend; /**< \brief Transport used to sample. */
    uint32_t sequence;             /**< \brief Monotonic sample sequence. */
    uint64_t timestamp;            /**< \brief Sample time in milliseconds. */
} cdrom_drive_state_t;

/** \brief Significant GD-ROM media or drive event type.
    \ingroup gdrom
*/
typedef enum cdrom_media_event_type {
    CDROM_MEDIA_EVENT_INSERTED,    /**< \brief Media became available. */
    CDROM_MEDIA_EVENT_REMOVED,     /**< \brief Media became unavailable. */
    CDROM_MEDIA_EVENT_CHANGED,     /**< \brief Media changed or unit attention. */
    CDROM_MEDIA_EVENT_ERROR,       /**< \brief Drive entered error state. */
    CDROM_MEDIA_EVENT_FATAL,       /**< \brief Drive entered fatal state. */
    CDROM_MEDIA_EVENT_RECOVERED    /**< \brief Drive left error/fatal state. */
} cdrom_media_event_type_t;

/** \brief A GD-ROM media or drive transition.
    \ingroup gdrom
*/
typedef struct cdrom_media_event {
    cdrom_media_event_type_t type; /**< \brief Kind of transition. */
    cdrom_drive_state_t previous;  /**< \brief State before the transition. */
    cdrom_drive_state_t current;   /**< \brief State after the transition. */
} cdrom_media_event_t;

/** \brief Callback for GD-ROM media and drive-error events.
    \ingroup gdrom

    Callbacks run serially on a dedicated KOS thread after the drive releases
    G1 ownership. They may use normal GD-ROM APIs, but must return promptly and
    must not wait indefinitely. They must not add or remove media-event
    handlers or call cdrom_shutdown(). Both registration functions detect
    callback context and fail with `EDEADLK`. Shutdown waits for an in-progress
    callback so its code and user data cannot outlive the GD-ROM subsystem.
*/
typedef void (*cdrom_media_event_callback_t)(
    const cdrom_media_event_t *event, void *data);

/** \brief Read the latest drive-state sample without waiting for the drive.
    \ingroup gdrom

    This is the nonblocking status path. The first call starts the background
    media monitor without waiting for its first sample, so it can return
    `EAGAIN`; later calls only copy the latest snapshot. The monitor
    uses the BIOS by default and follows the direct transport when the ISO9660
    driver is explicitly switched to its direct backend. `state->backend`
    identifies the sampler used. Under normal drive availability, the snapshot
    is at most 100 milliseconds old. A long operation owning G1 can defer
    sampling without delaying that operation.
    In particular, a staged stream session owns G1 continuously and suspends
    sampling until the session ends; ordinary chained reads release G1 between
    segments. A media-significant stream/request failure is still reported
    without waiting for a new sample. Compare `state->timestamp` with
    timer_ms_gettime64() when freshness matters.

    \param state           Destination for the coherent cached snapshot.

    \retval 0             A snapshot was returned.
    \retval -1            No sample is available, monitoring is unavailable,
                          or `state` is NULL, with errno set to `EAGAIN`,
                          `ENODEV`, or `EINVAL` respectively.
*/
int cdrom_get_cached_drive_state(cdrom_drive_state_t *state);

/** \brief Register a media/drive-error event callback.
    \ingroup gdrom

    The first registration starts the background monitor if necessary.
    Registration does not synthesize an event for the current media. Use
    cdrom_get_cached_drive_state() when the initial state is required.

    Callbacks run on the media-monitor thread while handler membership is
    protected. They must be bounded and must not block on application work,
    perform lengthy console/VFS I/O, call cdrom_shutdown(), or add/remove
    media handlers. Copy the event or set a flag and defer that work to an
    application thread.

    \param callback       Callback to invoke for significant transitions.
    \param data           User data passed to `callback`.

    \return               A positive handler identifier, or -1 with errno set.
*/
int cdrom_media_event_handler_add(cdrom_media_event_callback_t callback,
                                  void *data);

/** \brief Remove a media/drive-error event callback.
    \ingroup gdrom

    On success, any callback already in progress has returned and `data` is no
    longer referenced. This function must not be called by a media callback.

    \param handle         Identifier returned by
                          cdrom_media_event_handler_add().

    \retval 0             Handler removed.
    \retval -1            Invalid handle or callback-context call, with errno
                          set to `EINVAL`, `ENOENT`, or `EDEADLK`.
*/
int cdrom_media_event_handler_remove(int handle);

/** \brief    Change the datatype of disc.
    \ingroup  gdrom

    This function will take in all parameters to pass to the change_datatype
    syscall. This allows these parameters to be modified without a reinit.
    Each parameter allows -1 as a default, which is tied to the former static
    values provided by cdrom_reinit and cdrom_set_sector_size.

    \param sector_part      How much of each sector to return.
    \param track_type       What CDXA mode to read as (if applicable).
    \param sector_size      What sector size to read (eg. - 2048, 2532).

    \return                 \ref cd_cmd_response
    \see    cd_read_sector_part
*/
int cdrom_change_datatype(cd_read_sec_part_t sector_part, int track_type, int sector_size);

/** \brief    Re-initialize the GD-ROM drive.
    \ingroup  gdrom

    This function is for reinitializing the GD-ROM drive after a disc change to
    its default settings. Calls cdrom_reinit(-1,-1,-1)

    \return                 \ref cd_cmd_response
    \see    cdrom_reinit_ex
*/
int cdrom_reinit(void);

/** \brief    Re-initialize the GD-ROM drive with custom parameters.
    \ingroup  gdrom

    At the end of each cdrom_reinit(), cdrom_change_datatype is called.
    This passes in the requested values to that function after
    reinitialization, as opposed to defaults.

    \param sector_part      How much of each sector to return.
    \param cdxa             What CDXA mode to read as (if applicable).
    \param sector_size      What sector size to read (eg. - 2048, 2532).

    \return                 \ref cd_cmd_response
    \see    cd_read_sec_part_t
    \see    cdrom_change_datatype
*/
int cdrom_reinit_ex(cd_read_sec_part_t sector_part, int cdxa, int sector_size);

/** \brief    Read the table of contents from the disc.
    \ingroup  gdrom

    This function reads the TOC from the specified area of the disc.
    On regular CD-ROMs, there are only low density area.

    \param  toc_buffer      Space to store the returned TOC in.
    \param  high_density    Whether to read from the high density area.
    \return                 \ref cd_cmd_response
*/
int cdrom_read_toc(cd_toc_t *toc_buffer, bool high_density);

/** \brief    Read one or more sector from a CD-ROM.
    \ingroup  gdrom

    This function reads the specified number of sectors from the disc, starting
    where requested. This will respect the size of the sectors set with
    cdrom_change_datatype(). The buffer must have enough space to store the
    specified number of sectors and size must be a multiple of 32 for DMA.

    \param  buffer          Space to store the read sectors.
    \param  sector          The sector to start reading from.
    \param  cnt             The number of sectors to read.
    \param  dma             True for read using dma, false for pio.
    \return                 \ref cd_cmd_response

    \note                   If the buffer address points to the P2 memory area,
                            the caller function will be responsible for ensuring
                            memory coherency.

*/
int cdrom_read_sectors_ex(void *buffer, uint32_t sector, size_t cnt, bool dma);

/** \brief    Read one or more sector from a CD-ROM in PIO mode.
    \ingroup  gdrom

    Default version of cdrom_read_sectors_ex, which forces PIO mode.

    \param  buffer          Space to store the read sectors.
    \param  sector          The sector to start reading from.
    \param  cnt             The number of sectors to read.
    \return                 \ref cd_cmd_response
    \see    cdrom_read_sectors_ex
*/
int cdrom_read_sectors(void *buffer, uint32_t sector, size_t cnt);

/** \brief    Read one or more sectors asynchronously using GD DMA.
    \ingroup  gdrom

    This submits a GD-ROM request and returns before the transfer
    completes. The destination must remain valid, 32-byte aligned, and must not
    be accessed by the caller until the request reaches a terminal state. The
    request status exposes separate requested-data, useful-data, and physical
    I/O totals plus live logical and physical progress, in addition to the raw
    BIOS status. An optional callback is dispatched separately after the data
    is published. On any terminal state, the first `completed_bytes` in the
    destination are valid; subsequent bytes have undefined contents.

    \param  buffer          Space to store the read sectors.
    \param  sector          The sector to start reading from (FAD).
    \param  cnt             The number of sectors to read.
    \param  timeout         Timeout after the command starts, in milliseconds,
                            or zero for no timeout.
    \param  callback        Optional completion callback in thread context.
    \param  callback_data   User data passed to the callback.

    \return                 A request handle, or NULL with errno set.

    \note                   The transfer uses the sector size selected by
                            cdrom_change_datatype(). For P2 destinations, the
                            caller remains responsible for memory coherency.
*/
cdrom_request_t *cdrom_read_sectors_async(
    void *buffer, uint32_t sector, size_t cnt, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief    Move the GD-ROM pickup asynchronously.
    \ingroup  gdrom

    This submits a typed \ref CD_CMD_SEEK request for a disc FAD. It is useful
    as a scheduling hint before a later read and does not transfer data.

    \param  sector          Destination sector (FAD).
    \param  timeout         Timeout after the command starts, in milliseconds,
                            or zero for no timeout.
    \param  callback        Optional completion callback in thread context.
    \param  callback_data   User data passed to the callback.

    \return                 A request handle, or NULL with errno set.
*/
cdrom_request_t *cdrom_seek_async(
    uint32_t sector, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief    Start streaming from a CD-ROM.
    \ingroup  gdrom

    This function pre-reads the specified number of sectors from the disc.

    \param  sector          The sector to start reading from.
    \param  cnt             The number of sectors to read, 0x1ff means until end of disc.
    \param  dma             True for read using dma, false for pio.

    \return                 \ref cd_cmd_response
    \see    cdrom_transfer_request
*/
int cdrom_stream_start(int sector, int cnt, bool dma);

/** \brief    Stop streaming from a CD-ROM.
    \ingroup  gdrom

    This function finishing stream commands.

    \param  abort_dma       Abort current G1 DMA transfer.

    \return                 \ref cd_cmd_response
    \see    cdrom_transfer_request
*/
int cdrom_stream_stop(bool abort_dma);

/** \brief    Request stream transfer.
    \ingroup  gdrom

    This function request data from stream.

    \param  buffer          Space to store the read sectors (DMA aligned to 32, PIO to 2).
    \param  size            The size in bytes to read (DMA min 32, PIO min 2).
    \param  block           True to block until DMA transfer completes.
    \return                 \ref cd_cmd_response
    \see    cdrom_stream_start
*/
int cdrom_stream_request(void *buffer, size_t size, bool block);

/** \brief    Check requested stream transfer.
    \ingroup  gdrom

    This function check requested stream transfer.

    \param  size            The transfered (if in progress) or remain size in bytes.
    \return                 1 - is in progress, 0 - done
    \see    cdrom_transfer_request
*/
int cdrom_stream_progress(size_t *size);

/** \brief    Setting up a callback for transfers.
    \ingroup  gdrom

    This callback is called for every transfer request that is completed.

    \param  callback        Callback function.
    \param  param           Callback function param.
    \see    cdrom_transfer_request
*/
void cdrom_stream_set_callback(cdrom_stream_callback_t callback, void *param);

/** \brief    Read subcode data from the most recently read sectors.
    \ingroup  gdrom

    After reading sectors, this can pull subcode data regarding the sectors
    read. If reading all subcode data with CD_SUB_CURRENT_POSITION, this needs
    to be performed one sector at a time.

    \param  buffer          Space to store the read subcode data.
    \param  buflen          Amount of data to be read.
    \param  which           Which subcode type do you wish to get.

    \return                 \ref cd_cmd_response
    \see    cd_sub_type_t
*/
int cdrom_get_subcode(void *buffer, size_t buflen, cd_sub_type_t which);

/** \brief Decoded CDDA playback position and Q-subcode state.
    \ingroup gdrom

    `track_elapsed_frames` is relative to the current track. `fad` is the
    absolute frame address reported by the drive. The minute/second/frame
    fields are a convenience decomposition of `track_elapsed_frames` at 75
    frames per second.
*/
typedef struct cdrom_cdda_status {
    cd_sub_audio_t audio_status; /**< \brief BIOS CDDA playback state. */
    uint8_t control;             /**< \brief Q-channel control nibble. */
    uint8_t adr;                 /**< \brief Q-channel address nibble. */
    uint8_t track;               /**< \brief Current track number. */
    uint8_t index;               /**< \brief Current index within the track. */
    uint32_t track_elapsed_frames; /**< \brief Track-relative frame count. */
    uint32_t track_minutes;      /**< \brief Track-relative whole minutes. */
    uint32_t track_seconds;      /**< \brief Second within the minute. */
    uint32_t track_frames;       /**< \brief Frame within the second. */
    uint32_t fad;                /**< \brief Absolute frame address. */
} cdrom_cdda_status_t;

/** \brief Read and decode the current CDDA playback status synchronously.
    \ingroup gdrom

    This is the typed counterpart to reading `CD_SUB_Q_CHANNEL` with
    cdrom_get_subcode(). It reports playback state, track/index, track-relative
    time, and absolute FAD without exposing the BIOS byte layout.

    \param status          Destination for the decoded status.

    \return                \ref cd_cmd_response.
*/
int cdrom_cdda_get_status(cdrom_cdda_status_t *status);

/** \brief Queue a typed CDDA playback-status query.
    \ingroup gdrom

    `status` is populated during driver finalization before the request becomes
    terminal or its callback runs. It must remain valid until completion and
    has undefined contents unless the request completes successfully.

    \param status          Destination for the decoded status.
    \param timeout         Command timeout in milliseconds, or zero for none.
    \param callback        Optional completion callback in thread context.
    \param callback_data   User data passed to the callback.

    \return                A queued request, or NULL with errno set.
*/
cdrom_request_t *cdrom_cdda_get_status_async(
    cdrom_cdda_status_t *status, uint32_t timeout,
    cdrom_request_callback_t callback, void *callback_data);

/** \brief    Locate the sector of the data track.
    \ingroup  gdrom

    This function will search the toc for the last entry that has a CTRL value
    of 4, and return its FAD address.

    \param  toc             The TOC to search through.
    \return                 The FAD of the track, or 0 if none is found.
*/
uint32_t cdrom_locate_data_track(cd_toc_t *toc);

/** \brief    Play CDDA audio tracks or sectors.
    \ingroup  gdrom

    This function starts playback of CDDA audio.

    \param  start           The track or sector to start playback from.
    \param  end             The track or sector to end playback at.
    \param  loops           The number of times to repeat (max of 15).
    \param  mode            The mode to play (see \ref cdda_read_modes).
    \return                 \ref cd_cmd_response
*/
int cdrom_cdda_play(uint32_t start, uint32_t end, uint32_t loops, int mode);

/** \brief    Pause CDDA audio playback.
    \ingroup  gdrom

    \return                 \ref cd_cmd_response
*/
int cdrom_cdda_pause(void);

/** \brief    Resume CDDA audio playback after a pause.
    \ingroup  gdrom

    \return                 \ref cd_cmd_response
*/
int cdrom_cdda_resume(void);

/** \brief    Spin down the CD.
    \ingroup  gdrom

    This stops the disc in the drive from spinning until it is accessed again.

    \return                 \ref cd_cmd_response
*/
int cdrom_spin_down(void);

/** \brief    Initialize the GD-ROM for reading CDs.
    \ingroup  gdrom

    This initializes the CD-ROM reading system, reactivating the drive and
    handling initial setup of the disc.
*/
void cdrom_init(void);

/** \brief    Shutdown the CD reading system.
    \ingroup  gdrom
 */
void cdrom_shutdown(void);

__END_DECLS

#endif  /* __DC_CDROM_H */
