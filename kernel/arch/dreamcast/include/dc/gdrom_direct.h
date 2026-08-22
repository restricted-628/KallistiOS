/* KallistiOS ##version##

   dc/gdrom_direct.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __DC_GDROM_DIRECT_H
#define __DC_GDROM_DIRECT_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dc/cdrom.h>

/** \brief Byte size of one cooked data sector returned by direct reads. */
#define GDROM_DIRECT_SECTOR_SIZE     2048u

/** \brief Maximum sectors accepted by one bounded direct PIO read. */
#define GDROM_DIRECT_PIO_MAX_SECTORS 16u

/** \brief Maximum sectors accepted by one bounded direct DMA diagnostic. */
#define GDROM_DIRECT_DMA_MAX_SECTORS 16u

/** \file    dc/gdrom_direct.h
    \brief   Experimental direct GD-ROM diagnostic access.
    \ingroup gdrom

    These routines use the GD-ROM drive's packet interface directly,
    without submitting a command to the Dreamcast BIOS. Calling one is an
    explicit opt-in operation. The ISO9660 `/cd` driver can also be opted into
    this transport with fs_iso9660_set_backend(); its default remains the BIOS
    command server.

    Direct commands share KOS's G1 controller ownership with the BIOS-backed
    GD-ROM and ATA drivers. PIO and DMA have emulator controls, but these APIs
    remain opt-in until physical-drive timing, recovery, and GD-media behavior
    are validated.

    "Direct" means no BIOS GD command-server syscall is used for the operation.
    It does not replace the boot ROM's power-on initialization or GD-media
    authorization state; cold-boot drive bring-up is a separate layer.

    A supplied timeout bounds the primary command or sequence. Mandatory
    cleanup has its own fixed bound: a stranded command may require SPI soft
    reset, and CHECK acknowledgement may require `REQ_ERROR`, before G1 can be
    released safely. A failing call can therefore return after the primary
    deadline while completing that bounded recovery work.

    @{
*/

/** \brief Direct packet-transport phase reached by a diagnostic command. */
typedef enum gdrom_direct_phase {
    GDROM_DIRECT_PHASE_NONE = 0,
    GDROM_DIRECT_PHASE_WAIT_IDLE,
    GDROM_DIRECT_PHASE_WAIT_PACKET,
    GDROM_DIRECT_PHASE_DATA_IN,
    GDROM_DIRECT_PHASE_WAIT_DMA,
    GDROM_DIRECT_PHASE_COMPLETE,
    GDROM_DIRECT_PHASE_DATA_OUT
} gdrom_direct_phase_t;

/** \brief Packet command most recently attempted by a readiness probe. */
typedef enum gdrom_direct_probe_command {
    GDROM_DIRECT_PROBE_NONE = 0,
    GDROM_DIRECT_PROBE_TEST_UNIT,
    GDROM_DIRECT_PROBE_REQ_ERROR,
    GDROM_DIRECT_PROBE_REQ_STAT
} gdrom_direct_probe_command_t;

/** \brief Exact 2048-byte sector format requested by a direct read. */
typedef enum gdrom_direct_sector_type {
    GDROM_DIRECT_SECTOR_MODE1 = 0,
    GDROM_DIRECT_SECTOR_MODE2_FORM1
} gdrom_direct_sector_type_t;

/** \brief SPI CD-ROM data-rate selection used by the direct mode page. */
typedef enum gdrom_direct_speed {
    GDROM_DIRECT_SPEED_MAXIMUM  = 0,
    GDROM_DIRECT_SPEED_STANDARD = 1,
    GDROM_DIRECT_SPEED_2X       = 2,
    GDROM_DIRECT_SPEED_4X       = 3,
    GDROM_DIRECT_SPEED_6X       = 4,
    GDROM_DIRECT_SPEED_8X       = 5,
    GDROM_DIRECT_SPEED_10X      = 6,
    GDROM_DIRECT_SPEED_12X      = 7
} gdrom_direct_speed_t;

/** \brief Writable fields of the direct SPI hardware-information page. */
typedef struct gdrom_direct_mode_settings {
    gdrom_direct_speed_t speed; /**< \brief Requested data-read rate. */
    uint16_t standby_seconds;   /**< \brief Pause-to-standby delay; zero disables. */
    bool read_continuous;       /**< \brief Prefer continuity over error recovery. */
    bool ecc_retry;             /**< \brief Retry an ECC processing error. */
    bool read_retry;            /**< \brief Enable ordinary sector-read retry. */
    bool form2_retry;           /**< \brief Enable Mode-2 Form-2 retry. */
    uint8_t read_retry_count;   /**< \brief Same-sector retry count. */
} gdrom_direct_mode_settings_t;

/** \brief Complete decoded 32-byte direct SPI hardware-information page. */
typedef struct gdrom_direct_mode {
    gdrom_direct_mode_settings_t settings; /**< \brief Current writable fields. */
    char drive_information[9]; /**< \brief Eight-byte drive name plus terminator. */
    char system_version[9];    /**< \brief Eight-byte firmware version plus terminator. */
    char system_date[7];       /**< \brief Six-byte firmware date plus terminator. */
} gdrom_direct_mode_t;

/** \brief Low-level observations from a direct SPI transaction. */
typedef struct gdrom_direct_result {
    gdrom_direct_phase_t phase; /**< \brief Last transport phase reached. */
    uint8_t ata_status;         /**< \brief Final/most recent ATA status. */
    uint8_t ata_error;          /**< \brief ATA error register on CHECK. */
    uint8_t interrupt_reason;   /**< \brief Most recent CoD/IO value. */
    uint16_t device_byte_count; /**< \brief Most recent PIO group size. */
    size_t transferred;         /**< \brief PIO bytes transferred in either direction. */
    uint32_t dma_event;         /**< \brief Holly DMA completion/error event. */
    uint32_t dma_current_address; /**< \brief Final `SB_GDSTARD` value. */
    size_t dma_transferred;     /**< \brief Final `SB_GDLEND` value. */
    bool command_event;         /**< \brief Drive command INTRQ was observed. */
    bool dma_event_seen;        /**< \brief A Holly GD-DMA event was observed. */
    bool recovery_attempted;    /**< \brief SPI soft reset was required. */
    bool recovery_succeeded;    /**< \brief SPI soft reset restored the drive. */
    bool sense_valid;           /**< \brief CHECK sense was captured atomically. */
    cdrom_sense_t sense;        /**< \brief Sense captured before releasing G1. */
    uint32_t command_specific_information; /**< \brief CHECK command detail. */
} gdrom_direct_result_t;

/** \brief Typed ten-byte response from the SPI REQ_STAT command. */
typedef struct gdrom_direct_status {
    cd_stat_t status;
    cd_disc_types_t disc_type;
    uint8_t repeat_count;
    uint8_t control;
    uint8_t adr;
    uint8_t track;
    uint8_t index;
    uint32_t fad;
    uint8_t max_read_retries;
} gdrom_direct_status_t;

/** \brief Typed ten-byte response from the SPI REQ_ERROR command. */
typedef struct gdrom_direct_error {
    cdrom_sense_t sense;    /**< \brief Drive sense key, ASC, and ASCQ. */
    uint32_t command_specific_information; /**< \brief SPI bytes 4 through 7. */
} gdrom_direct_error_t;

/** \brief Typed six-byte response from the SPI REQ_SES command. */
typedef struct gdrom_direct_session {
    cd_stat_t status;          /**< \brief Drive state returned with the data. */
    uint8_t requested_session; /**< \brief Session number supplied to REQ_SES. */
    uint8_t session_count;     /**< \brief Total sessions when request was zero. */
    uint8_t first_track;       /**< \brief First track of a nonzero session. */
    uint32_t fad;              /**< \brief Lead-out (zero) or session start FAD. */
} gdrom_direct_session_t;

/** \brief Results from a bounded direct-drive readiness probe.

    The probe distinguishes transport failure from a diagnosed drive state.
    A successful function return means the sequence completed and `result`
    describes the drive outcome; it does not require readable media.
*/
typedef struct gdrom_direct_probe_result {
    gdrom_direct_status_t status; /**< \brief Last decoded drive status. */
    gdrom_direct_error_t error;   /**< \brief Last decoded CHECK details. */
    gdrom_direct_result_t test_unit_transport; /**< \brief TEST_UNIT trace. */
    gdrom_direct_result_t status_transport; /**< \brief Last REQ_STAT trace. */
    gdrom_direct_result_t error_transport; /**< \brief Last REQ_ERROR trace. */
    int result;                 /**< \brief Stable KOS `ERR_*` result. */
    gdrom_direct_probe_command_t last_command; /**< \brief Last attempted command. */
    uint8_t status_requests;    /**< \brief Number of REQ_STAT commands. */
    uint8_t error_requests;     /**< \brief Number of REQ_ERROR commands. */
    bool status_valid;          /**< \brief Status response was decoded. */
    bool error_valid;           /**< \brief Error response was decoded. */
} gdrom_direct_probe_result_t;

/** \brief Result of a post-boot direct-drive reinitialization sequence. */
typedef struct gdrom_direct_reinit_result {
    gdrom_direct_result_t reset_transport; /**< \brief SPI soft-reset trace. */
    gdrom_direct_probe_result_t probe; /**< \brief Post-reset readiness/state. */
} gdrom_direct_reinit_result_t;

/** \brief Results from the controlled direct-DMA recovery diagnostic.

    The diagnostic deliberately aborts one DMA immediately after start, proves
    that direct PIO remains usable, requests one DMA with a protection window
    that excludes its otherwise-valid destination, and finally proves that a
    normal direct DMA still succeeds. It never accepts a caller-selected bad
    address.
*/
typedef struct gdrom_direct_dma_diagnostic {
    gdrom_direct_result_t abort_transport; /**< \brief Forced-abort trace. */
    gdrom_direct_result_t post_abort_transport; /**< \brief PIO reuse trace. */
    gdrom_direct_result_t protection_transport; /**< \brief Fault trace. */
    gdrom_direct_result_t final_transport; /**< \brief Final DMA reuse trace. */
    int abort_error;              /**< \brief Expected `ECANCELED`. */
    int protection_error;         /**< \brief Expected protection failure. */
    bool abort_reuse_succeeded;   /**< \brief PIO worked after abort. */
    bool protection_fault_observed; /**< \brief Holly rejected the window. */
    bool final_read_succeeded;    /**< \brief DMA worked after both tests. */
} gdrom_direct_dma_diagnostic_t;

/** \brief Issue the direct, non-data TEST_UNIT packet.

    A CHECK response is reported as EIO and preserved in result. This can be
    expected when no readable medium is present; it still proves that the
    packet transport reached the drive.

    \param  timeout     Required nonzero whole-operation timeout in milliseconds.
    \param  result      Optional low-level diagnostic observations.
    \retval 0           Command completed without CHECK.
    \retval -1          Failure, with errno set.
*/
int gdrom_direct_test_unit(uint32_t timeout, gdrom_direct_result_t *result);

/** \brief Read and decode the current direct-drive error details.

    REQ_ERROR acknowledges the current sense condition in the drive. When a
    result object is supplied, normal direct operations capture CHECK sense
    atomically in that object's `sense` fields before releasing G1. This
    standalone entry point is for explicit diagnostics and commands that did
    not retain such a result; do not issue it speculatively between unrelated
    commands.

    \param  error       Output for the decoded error response.
    \param  timeout     Required nonzero whole-operation timeout in milliseconds.
    \param  result      Optional low-level diagnostic observations.
    \retval 0           Error details were read and decoded.
    \retval -1          Transport or protocol failure, with errno set.
*/
int gdrom_direct_get_error(gdrom_direct_error_t *error, uint32_t timeout,
                           gdrom_direct_result_t *result);

/** \brief Read and decode drive state with a direct SPI REQ_STAT packet.

    This is the primary safe transport probe: it does not seek, spin up, read
    sectors, alter mode settings, or replace the BIOS-backed driver.

    If a complete ten-byte payload is followed by CHECK, `status` is still
    populated and the function returns -1 with errno set to `EIO`. The
    transport trace makes that case distinguishable from a partial response.

    \param  status      Output for the decoded drive state.
    \param  timeout     Required nonzero whole-operation timeout in milliseconds.
    \param  result      Optional low-level diagnostic observations.
    \retval 0           Status was read and decoded.
    \retval -1          Failure, with errno set.
*/
int gdrom_direct_get_status(gdrom_direct_status_t *status, uint32_t timeout,
                            gdrom_direct_result_t *result);

/** \brief Read and decode the direct drive's SPI hardware-information page.

    This is distinct from \ref cdrom_change_datatype, which controls the
    logical sector layout returned by the BIOS command server. The optional
    speed values are reported exactly as supplied by the drive; unsupported
    selections may be rejected by the device.
*/
int gdrom_direct_get_mode(gdrom_direct_mode_t *mode, uint32_t timeout,
                          gdrom_direct_result_t *result);

/** \brief Replace all writable direct SPI hardware-information fields.

    KOS first reads the current 32-byte page, then sends only its writable byte
    range (bytes 2-9).
    Read-only drive identification, firmware version, and date bytes are never
    written. The sequence owns G1 continuously so no other drive command can
    interleave between the read and update.
*/
int gdrom_direct_set_mode(const gdrom_direct_mode_settings_t *settings,
                          uint32_t timeout,
                          gdrom_direct_result_t *result);

/** \brief Move the GD-ROM pickup to an absolute frame through direct SPI.

    This issues the non-data `CD_SEEK` packet with FAD addressing. It is a
    positioning hint for a later read and does not transfer data.

    \param  fad          Destination absolute frame address, at least 150.
    \param  timeout      Required nonzero command timeout in milliseconds.
    \param  result       Optional direct transport and CHECK-sense result.
    \retval 0            The drive accepted and completed the seek.
    \retval -1           Failure, with errno set.
*/
int gdrom_direct_seek(uint32_t fad, uint32_t timeout,
                      gdrom_direct_result_t *result);

/** \brief Perform a bounded TEST_UNIT/error/status direct-drive probe.

    The sequence acknowledges CHECK with REQ_ERROR and retries REQ_STAT once.
    Primary probe commands share one timeout deadline; mandatory CHECK
    acknowledgement uses the separate bounded cleanup rule described for this
    header. `result->result` reports `ERR_NO_DISC`, `ERR_DISC_CHG`, and other
    diagnosed drive states while the function itself returns success when the
    direct transport completed.

    \param  result      Complete probe outputs and per-command traces.
    \param  timeout     Required nonzero whole-sequence timeout in milliseconds.
    \retval 0           Probe completed; inspect `result->result` and status.
    \retval -1          Transport/protocol failure, with errno set.
*/
int gdrom_direct_probe(gdrom_direct_probe_result_t *result, uint32_t timeout);

/** \brief Reinitialize the already boot-authorized drive without BIOS calls.

    This performs the SPI-defined 08h soft reset, then a bounded direct
    readiness/error/status probe which acknowledges the expected post-reset
    unit-attention condition. It restores the drive's default mode
    parameters and leaves the resulting media state in `result->probe`.

    This is post-boot transport reinitialization. It does not perform the boot
    ROM's power-on hardware sequence, GD security/license authorization, or an
    ISO9660 mount, and it must not be presented as cold-boot drive bring-up.

    A successful return means the reset and probe sequence completed; inspect
    `result->probe.result` for `ERR_NO_DISC`, `ERR_DISC_CHG`, or another
    diagnosed media state.
*/
int gdrom_direct_reinitialize(gdrom_direct_reinit_result_t *result,
                              uint32_t timeout);

/** \brief Read a disc table of contents through direct SPI GET_TOC.

    The 408-byte SPI response is decoded into KOS's existing \ref cd_toc_t.
    Existing `TOC_*` accessors and \ref cdrom_locate_data_track therefore work
    identically for BIOS-backed and direct TOCs.

    \param  toc          Output for all 99 track slots, first/last, and lead-out.
    \param  high_density False for the single-density area, true for GD high density.
    \param  timeout      Required nonzero whole-operation timeout in milliseconds.
    \param  result       Optional low-level diagnostic observations.
    \retval 0            Complete TOC was read and decoded.
    \retval -1           Transport or protocol failure, with errno set.
*/
int gdrom_direct_read_toc(cd_toc_t *toc, bool high_density,
                          uint32_t timeout, gdrom_direct_result_t *result);

/** \brief Read subcode through a direct SPI GET_SCD command.

    `which` and the returned byte layout are the same KOS contract as
    \ref cdrom_get_subcode. The direct command requests exactly `buflen`
    bytes and succeeds only when that complete response was transferred.

    \param buffer       Destination for the raw subcode response.
    \param buflen       Required byte count from 1 through 65535.
    \param which        Raw, Q-channel, media-catalog, or ISRC format.
    \param timeout      Required nonzero command timeout in milliseconds.
    \param result       Optional direct transport and CHECK-sense result.
    \retval 0           The requested response was transferred completely.
    \retval -1          Failure, with errno set.
*/
int gdrom_direct_get_subcode(
    void *buffer, size_t buflen, cd_sub_type_t which, uint32_t timeout,
    gdrom_direct_result_t *result);

/** \brief Read typed CDDA playback state through direct SPI.

    This is the direct counterpart to \ref cdrom_cdda_get_status and fills the
    same transport-independent KOS structure from the 14-byte Q-channel
    response.
*/
int gdrom_direct_cdda_get_status(
    cdrom_cdda_status_t *status, uint32_t timeout,
    gdrom_direct_result_t *result);

/** \brief Start CDDA playback through direct SPI.

    `mode` accepts the existing `CDDA_TRACKS` and `CDDA_SECTORS` values. Sector
    mode sends `start` and `end` as FADs directly. Track mode resolves the
    inclusive range through the single-density or GD high-density TOC while
    retaining G1 ownership through TOC lookup and playback. A range cannot
    cross the two discontinuous areas. `loops` is 0 through 15, where 15 is
    endless. An `end` FAD of zero means play to lead-out.
*/
int gdrom_direct_cdda_play(
    uint32_t start, uint32_t end, uint32_t loops, int mode,
    uint32_t timeout, gdrom_direct_result_t *result);

/** \brief Pause current CDDA playback through direct SPI CD_SEEK. */
int gdrom_direct_cdda_pause(uint32_t timeout,
                            gdrom_direct_result_t *result);

/** \brief Resume the existing CDDA range through direct SPI CD_PLAY.

    The range and repeat mode established by the preceding play command are
    retained. The firmware resume operation has no effective repeat update, so
    this API does not expose a misleading argument.
*/
int gdrom_direct_cdda_resume(uint32_t timeout,
                             gdrom_direct_result_t *result);

/** \brief Stop CDDA playback and return the pickup home through direct SPI. */
int gdrom_direct_cdda_stop(uint32_t timeout,
                           gdrom_direct_result_t *result);

/** \brief Start direct CDDA scan playback.

    `reverse` selects fast-reverse instead of fast-forward. The raw SPI `speed`
    selects 2x for 0-2, 5x for 3-5, 9x for 6-9, and 16x for 10-255.
*/
int gdrom_direct_cdda_scan(bool reverse, uint8_t speed, uint32_t timeout,
                           gdrom_direct_result_t *result);

/** \brief Read direct SPI session geometry.

    Session zero reports `session_count` and the final lead-out FAD. A request
    from one through 99 reports `first_track` and that session's starting FAD.
    The unused count/track field is set to zero.

    \param  session      Session zero (summary), or a session from 1 through 99.
    \param  info         Output for decoded status and geometry.
    \param  timeout      Required nonzero whole-operation timeout in milliseconds.
    \param  result       Optional low-level diagnostic observations.
    \retval 0            Session data was read and decoded.
    \retval -1           Transport or protocol failure, with errno set.
*/
int gdrom_direct_get_session(uint8_t session,
                             gdrom_direct_session_t *info,
                             uint32_t timeout,
                             gdrom_direct_result_t *result);

/** \brief Read cooked 2048-byte sectors through the direct PIO transport.

    This experimental operation issues one SPI `CD_READ` command without the
    BIOS command server. It requests the sector data field only and makes the
    drive verify either Mode-1 or Mode-2 Form-1 media, both of which transfer
    exactly 2048 payload bytes per sector.

    The caller must split larger operations. Limiting each command to sixteen
    sectors bounds continuous G1 ownership so other G1 clients can run between
    reads.

    \param  buffer       Destination aligned to at least two bytes.
    \param  fad          First absolute frame address; must be at least 150.
    \param  sectors      Required count from 1 through 16.
    \param  sector_type  Exact 2048-byte sector format expected from the disc.
    \param  timeout      Required nonzero whole-operation timeout in milliseconds.
    \param  result       Optional low-level diagnostic observations.
    \retval 0            Every requested sector was transferred.
    \retval -1           Failure, with errno set. On drive CHECK, `result`
                         contains atomically captured sense when non-NULL.

    A command stranded by timeout is recovered with the SPI `08h` soft reset.
    If recovery fails, shared G1 access fails closed until reboot.
*/
int gdrom_direct_read_sectors(void *buffer, uint32_t fad, size_t sectors,
                              gdrom_direct_sector_type_t sector_type,
                              uint32_t timeout,
                              gdrom_direct_result_t *result);

/** \brief Read cooked sectors through the direct Holly GD-DMA transport.

    This experimental operation does not use the BIOS command server. The
    destination must be 32-byte aligned system RAM. Success requires both the
    drive command INTRQ and the Holly DMA-complete event; neither interrupt by
    itself publishes the buffer.

    Cacheable destinations are invalidated immediately before DMA and again
    after the engine becomes inactive. The transport explicitly selects
    multiword-DMA mode 2 in both the drive and Holly. A timeout disables DMA,
    waits for it to stop, and performs a bounded SPI soft-reset recovery before
    G1 ownership is released. If either engine cannot be made reusable, shared
    G1 access fails closed until reboot.

    \param  buffer       Destination aligned to 32 bytes in system RAM.
    \param  fad          First absolute frame address; must be at least 150.
    \param  sectors      Required count from 1 through 16.
    \param  sector_type  Exact 2048-byte sector format expected from the disc.
    \param  timeout      Required nonzero whole-operation timeout in milliseconds.
    \param  result       Optional low-level command and DMA observations.
    \retval 0            Both command and DMA completed successfully.
    \retval -1           Failure, with errno set.
*/
int gdrom_direct_read_sectors_dma(
    void *buffer, uint32_t fad, size_t sectors,
    gdrom_direct_sector_type_t sector_type, uint32_t timeout,
    gdrom_direct_result_t *result);

/** \brief Exercise bounded direct-DMA abort and protection-fault recovery.

    This destructive controller diagnostic is intended for a dedicated test
    program, not application startup. `buffer` is always the only DMA
    destination and must satisfy the normal direct-DMA contract. The
    protection test changes only `SB_GDAPRO`: it deliberately excludes that
    valid buffer instead of programming an arbitrary physical address.

    The function performs, in order: a forced abort, a one-sector direct PIO
    reuse check, an excluded-protection-window DMA, and a final normal DMA.
    Every phase shares the supplied nonzero per-operation timeout. A return of
    `ENOTSUP` means the execution environment did not enforce/report the GD-DMA
    protection window; the final reuse attempt is still made when safe.

    \param  buffer       Destination aligned to 32 bytes in system RAM.
    \param  fad          First absolute frame address; must be at least 150.
    \param  sectors      Required count from 1 through 16.
    \param  sector_type  Exact 2048-byte sector format expected from the disc.
    \param  timeout      Required nonzero timeout for each diagnostic phase.
    \param  diagnostic   Complete result record; must not be `NULL`.
    \retval 0            All fault and post-fault checks behaved as expected.
    \retval -1           A check failed, with errno set and traces retained.
*/
int gdrom_direct_dma_diagnose(
    void *buffer, uint32_t fad, size_t sectors,
    gdrom_direct_sector_type_t sector_type, uint32_t timeout,
    gdrom_direct_dma_diagnostic_t *diagnostic);

/** @} */

__END_DECLS

#endif /* __DC_GDROM_DIRECT_H */
