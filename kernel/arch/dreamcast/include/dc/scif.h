/* KallistiOS ##version##

   dc/scif.h
   Copyright (C) 2000,2001,2004 Megan Potter
   Copyright (C) 2012 Lawrence Sebald
   Copyright (C) 2023 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/scif.h
    \brief   Serial port functionality.
    \ingroup system_scif

    This file deals with raw access to the serial port on the Dreamcast.

    \author Megan Potter
    \author Lawrence Sebald
    \author Ruslan Rostovtsev
*/

#ifndef __DC_SCIF_H
#define __DC_SCIF_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <kos/dbgio.h>

/** \defgroup system_scif   SCIF
    \brief                  Driver for managing the serial port
    \ingroup                system

    @{
*/

/** \brief  Default serial bitrate. */
#define DEFAULT_SERIAL_BAUD 115200

/** \brief  Default legacy-buffer flush behavior. */
#define DEFAULT_SERIAL_FIFO 1

/** \brief  SCIF parity selection. */
typedef enum scif_parity {
    SCIF_PARITY_NONE = 0,  /**< No parity bit. */
    SCIF_PARITY_EVEN,      /**< Even parity. */
    SCIF_PARITY_ODD        /**< Odd parity. */
} scif_parity_t;

/** \brief  SCIF modem-flow-control selection. */
typedef enum scif_flow_control {
    SCIF_FLOW_NONE = 0,    /**< Ignore CTS and do not drive automatic RTS. */
    SCIF_FLOW_HARDWARE     /**< Use the SCIF's automatic RTS/CTS control. */
} scif_flow_control_t;

/** \brief  SCIF receive-FIFO interrupt threshold. */
typedef enum scif_rx_trigger {
    SCIF_RX_TRIGGER_1 = 1,   /**< Interrupt at one received byte. */
    SCIF_RX_TRIGGER_4 = 4,   /**< Interrupt at four received bytes. */
    SCIF_RX_TRIGGER_8 = 8,   /**< Interrupt at eight received bytes. */
    SCIF_RX_TRIGGER_14 = 14  /**< Interrupt at fourteen received bytes. */
} scif_rx_trigger_t;

/** \brief  SCIF transmit-FIFO readiness threshold. */
typedef enum scif_tx_trigger {
    SCIF_TX_TRIGGER_1 = 1,  /**< Ready when at most one byte remains. */
    SCIF_TX_TRIGGER_2 = 2,  /**< Ready when at most two bytes remain. */
    SCIF_TX_TRIGGER_4 = 4,  /**< Ready when at most four bytes remain. */
    SCIF_TX_TRIGGER_8 = 8   /**< Ready when at most eight bytes remain. */
} scif_tx_trigger_t;

/** \brief  Checked SCIF line configuration. */
typedef struct scif_config {
    /** Requested bitrate, or 0 to use the external 16x serial clock. */
    uint32_t baud;
    /** Character width. Must be 7 or 8. */
    uint8_t data_bits;
    /** Stop-bit count. Must be 1 or 2. */
    uint8_t stop_bits;
    /** Parity selection. */
    scif_parity_t parity;
    /** Automatic modem-flow-control selection. */
    scif_flow_control_t flow_control;
    /** Receive-FIFO interrupt threshold. */
    scif_rx_trigger_t rx_trigger;
    /** Transmit-FIFO readiness threshold. */
    scif_tx_trigger_t tx_trigger;
} scif_config_t;

/** \brief  Coherent SCIF state and diagnostic snapshot. */
typedef struct scif_status {
    /** Currently configured line parameters. */
    scif_config_t config;
    /** Actual internal-clock bitrate, or 0 for an external clock. */
    uint32_t actual_baud;
    /** Signed difference between actual and requested bitrate, in ppm. */
    int32_t baud_error_ppm;
    /** Bytes ready for scif_read() without blocking. */
    size_t receive_queued;
    /** Bytes currently waiting in the hardware transmit FIFO. */
    size_t transmit_queued;
    /** Bytes discarded because the software receive ring was full. */
    uint32_t receive_dropped;
    /** Received characters carrying a framing error. */
    uint32_t framing_errors;
    /** Received characters carrying a parity error. */
    uint32_t parity_errors;
    /** Hardware receive-overrun events. */
    uint32_t overrun_errors;
    /** Received break events. */
    uint32_t breaks;
    /** Bounded transmit or flush waits that expired. */
    uint32_t transmit_timeouts;
    /** Incremented whenever an error, break, drop, or timeout is recorded. */
    uint32_t event_sequence;
    /** True when the receive IRQ ring is active. */
    bool irq_enabled;
    /** True when byte-oriented SCIF access is available. */
    bool enabled;
} scif_status_t;

/** \brief  Install a checked SCIF configuration immediately.

    This reinitializes the byte-oriented SCIF port. It fails rather than
    disturbing an active serial-loader or SCIF-SPI owner.

    \param  config         Configuration to validate and install.
    \retval 0              On success.
    \retval -1             On error with errno set.
*/
int scif_configure(const scif_config_t *config);

/** \brief  Return a coherent SCIF state and diagnostic snapshot.
    \param  status         Destination for the snapshot.
    \retval 0              On success.
    \retval -1             If status is NULL (errno set to EINVAL).
*/
int scif_get_status(scif_status_t *status);

/** \brief  Clear cumulative SCIF error and timeout counters. */
void scif_clear_stats(void);

/** \brief  Set serial parameters.

    This compatibility entry point selects 8-N-1 framing for the next
    scif_init() call. New code that needs checked framing or immediate
    installation should use scif_configure().

    \param  baud            The bitrate to set.
    \param  fifo            Non-zero to flush at the end of legacy buffered
                            writes; zero to leave transmission in progress.
*/
void scif_set_parameters(int baud, int fifo);

/* The rest of these are the standard dbgio interface. */

/** \brief  Enable or disable SCIF IRQ usage.
    \param  on              1 to enable IRQ usage, 0 for polled I/O.
    \retval 0               On success (no error conditions defined).
*/
int scif_set_irq_usage(int on);

/** \brief  Is the SCIF port detected? Of course it is!
    \return                 1
*/
int scif_detected(void);

/** \brief  Initialize the SCIF port.

    This function initializes the SCIF port to a sane state. If dcload-serial is
    in use, this is effectively a no-op.

    \retval 0               On success (no error conditions defined).
*/
int scif_init(void);

/** \brief  Shutdown the SCIF port.

    This function disables SCIF IRQs and byte transmission/reception. A later
    scif_init() restores the retained configuration.

    \retval 0               On success (no error conditions defined).
*/
int scif_shutdown(void);

/** \brief  Read a single character from the SCIF port.
    \return                 The character read if one is available, otherwise -1
                            with errno set to EAGAIN, EBUSY, or EIO.
*/
int scif_read(void);

/** \brief  Peek at the next received character without consuming it.

    In polled mode, the driver consumes one hardware-FIFO entry into an
    internal lookahead slot; the next scif_read() returns that same byte.

    \return                 The next byte, or -1 with errno set.
*/
int scif_peek(void);

/** \brief  Return the number of immediately readable bytes.
    \return                 A value from 0 through the receive-buffer capacity,
                            or -1 with errno set.
*/
int scif_read_available(void);

/** \brief  Read as many immediately available bytes as possible.

    This operation never waits. A return value of 0 means no data was ready.

    \param  data            Destination buffer.
    \param  len             Destination capacity.
    \return                 Number of bytes read, or -1 with errno set.
*/
int scif_read_buffer_nonblock(uint8_t *data, size_t len);

/** \brief  Write a single character to the SCIF port.
    \param  c               The character to write (only the low 8-bits are
                            written).
    \retval 1               On success.
    \retval -1              On timeout or if byte I/O is unavailable.
*/
int scif_write(int c);

/** \brief  Attempt to write one byte without waiting.
    \param  c               Character whose low eight bits are written.
    \retval 1               The byte was accepted by the hardware FIFO.
    \retval -1              No space or the port is unavailable; errno is set.
*/
int scif_try_write(int c);

/** \brief  Return the number of free hardware transmit-FIFO entries.
    \return                 A value from 0 through 16, or -1 with errno set.
*/
int scif_write_available(void);

/** \brief  Flush any FIFO'd bytes out of the buffer.

    This function sends any bytes that have been queued up for transmission but
    have not left yet in FIFO mode.

    \retval 0               On success.
    \retval -1              On timeout or if byte I/O is unavailable.
*/
int scif_flush(void);

/** \brief  Write a whole buffer of data to the SCIF port.

    This function writes a whole buffer of data to the SCIF port, optionally
    making all newlines into carriage return + newline pairs.

    \param  data            The buffer to write.
    \param  len             The length of the buffer, in bytes.
    \param  xlat            If set to 1, all newlines will be written as CRLF.
    \return                 The number of bytes written on success, -1 on error.
*/
int scif_write_buffer(const uint8_t *data, int len, int xlat);

/** \brief  Read a buffer of data from the SCIF port.

    This function reads a whole buffer of data from the SCIF port, blocking
    until it has been filled.

    \param  data            The buffer to read into.
    \param  len             The number of bytes to read.
    \return                 The number of bytes read on success, -1 on error.
*/
int scif_read_buffer(uint8_t *data, int len);

/** \brief  SCIF debug I/O handler. Do not modify! */
extern dbgio_handler_t dbgio_scif;

/* Low-level SPI related functionality below here... */
/** \brief  Initialize the SCIF port for use of an SPI peripheral.

    This function initializes the SCIF port for accessing the an SPI peripheral
    that has been connected to the serial port. The design of the SCIF->SPI
    wiring follows the wiring of the SD card adapter which is (at least now)
    somewhat commonly available online and is the same as the one designed by
    jj1odm.

    \retval 0               On success.
    \retval -1              On error (if dcload-serial is detected).
*/
int scif_spi_init(void);

/** \brief  Shut down SPI card support over the SCIF port.

    This function shuts down SPI support on the SCIF port. If you want to get
    regular usage of the port back, you must call scif_init() after shutting
    down SPI support.

    \retval 0               On success (no errors defined).
*/
int scif_spi_shutdown(void);

/** \brief  Set or clear the SPI /CS line.

    This function sets or clears the /CS line (connected to the RTS line of the
    SCIF port).

    \param  v               Non-zero to output 1 on the line, zero to output 0.
*/
void scif_spi_set_cs(int v);

/** \brief  Read and write one byte from the SPI port.

    This function writes one byte and reads one back from the SPI device
    simultaneously.

    \param  b               The byte to write out to the port.
    \return                 The byte returned from the card.
*/
uint8_t scif_spi_rw_byte(uint8_t b);

/** \brief  Read and write one byte from the SPI device, slowly.

    This function does the same thing as the scif_sd_rw_byte() function, but
    with a 1.5usec delay between asserting the CLK line and reading back the bit
    and a 1.5usec delay between clearing the CLK line and writing the next bit
    out.

    This ends up working out to a clock of about 333khz, or so.

    \param  b               The byte to write out to the port.
    \return                 The byte returned from the card.
*/
uint8_t scif_spi_slow_rw_byte(uint8_t b);


/** \brief  Write a byte to the SPI device.

    This function writes out the specified byte to the SPI device, one bit at a
    time. The timing follows that of the scif_spi_rw_byte() function.

    \param  b               The byte to write out to the port.
*/
void scif_spi_write_byte(uint8_t b);

/** \brief  Write data to the SPI device.

    This function writes data to the SPI device. The bulk of the buffer is
    written four bytes at a time; any unaligned leading bytes and leftover
    trailing bytes are written one byte at a time.

    \param  buffer          Buffer to write data from.
    \param  len             Number of bytes to write to the device.
*/
void scif_spi_write_data(const uint8_t *buffer, size_t len);

/** \brief  Read a byte from the SPI device.

    This function reads a byte from the SPI device, one bit at a time. Timing
    is similar to (but slightly faster than) the scif_spi_rw_byte() function.

    \return                 The byte returned from the device.
*/
uint8_t scif_spi_read_byte(void);

/** \brief  Read data from the SPI device.

    This function reads data from the SPI device. If the buffer is aligned and
    len is divisible by 4, optimizations are applied.

    \param  buffer          Buffer to store read data into.
    \param  len             Number of bytes to read from the device.
*/
void scif_spi_read_data(uint8_t *buffer, size_t len);

/** @} */

__END_DECLS

#endif  /* __DC_SCIF_H */
