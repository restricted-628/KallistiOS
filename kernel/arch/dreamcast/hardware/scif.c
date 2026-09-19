/* KallistiOS ##version##

   hardware/scif.c
   Copyright (C) 2000, 2001, 2004 Megan Potter
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch/arch.h>
#include <dc/fs_dcload.h>
#include <dc/scif.h>
#include <kos/dbgio.h>
#include <kos/irq.h>

#include "scif_config_internal.h"

/*
   The byte-oriented driver is allocation-free and worker-free: receive IRQs
   drain the 16-byte hardware FIFO into a fixed software ring, while transmit
   operations either poll with a legacy bound or use nonblocking readiness.
   SCIF-SPI temporarily owns the register bank through the private claim API.
*/

#define SCIFREG08(x) *((volatile uint8_t *)(x))
#define SCIFREG16(x) *((volatile uint16_t *)(x))
#define SCSMR2  SCIFREG16(0xffe80000)
#define SCBRR2  SCIFREG08(0xffe80004)
#define SCSCR2  SCIFREG16(0xffe80008)
#define SCFTDR2 SCIFREG08(0xffe8000c)
#define SCFSR2  SCIFREG16(0xffe80010)
#define SCFRDR2 SCIFREG08(0xffe80014)
#define SCFCR2  SCIFREG16(0xffe80018)
#define SCFDR2  SCIFREG16(0xffe8001c)
#define SCSPTR2 SCIFREG16(0xffe80020)
#define SCLSR2  SCIFREG16(0xffe80024)

#define SCSCR_TIE   0x80u
#define SCSCR_RIE   0x40u
#define SCSCR_TE    0x20u
#define SCSCR_RE    0x10u
#define SCSCR_REIE  0x08u

#define SCFSR_ER    0x80u
#define SCFSR_TEND  0x40u
#define SCFSR_TDFE  0x20u
#define SCFSR_BRK   0x10u
#define SCFSR_FER   0x08u
#define SCFSR_PER   0x04u
#define SCFSR_RDF   0x02u
#define SCFSR_DR    0x01u

#define SCFCR_TFRST 0x04u
#define SCFCR_RFRST 0x02u

#define SCLSR_ORER  0x01u

#define SCIF_FIFO_CAPACITY  16u
#define SCIF_LEGACY_SPINS   800000
#define SCIF_RX_BUFFER_SIZE 1024u

/*
   serial_config is the retained configuration requested for the next init.
   active_config and register_config describe hardware that was actually
   installed; keeping them separate makes the status snapshot truthful after
   the compatibility setter changes a pending bitrate.
*/
static scif_config_t serial_config = {
    .baud = DEFAULT_SERIAL_BAUD,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = SCIF_PARITY_NONE,
    .flow_control = SCIF_FLOW_NONE,
    .rx_trigger = SCIF_RX_TRIGGER_4,
    .tx_trigger = SCIF_TX_TRIGGER_8
};

static scif_config_t active_config = {
    .baud = DEFAULT_SERIAL_BAUD,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = SCIF_PARITY_NONE,
    .flow_control = SCIF_FLOW_NONE,
    .rx_trigger = SCIF_RX_TRIGGER_4,
    .tx_trigger = SCIF_TX_TRIGGER_8
};

static scif_register_config_t register_config;
static bool serial_fifo = DEFAULT_SERIAL_FIFO;
static bool serial_enabled;
static bool serial_initialized;
static bool scif_irq_usage;
static bool spi_active;
static bool spi_saved_irq_usage;

/*
   Only the receive interrupt producer and interrupt-excluded application
   consumers mutate this ring. Full rings discard the arriving byte instead
   of overwriting unread data or allowing the occupancy count to escape its
   bounds.
*/
static uint8_t recvbuf[SCIF_RX_BUFFER_SIZE];
static size_t rb_head;
static size_t rb_tail;
static size_t rb_count;
static bool polled_peek_valid;
static uint8_t polled_peek_value;

typedef struct scif_stats {
    uint32_t receive_dropped;
    uint32_t framing_errors;
    uint32_t parity_errors;
    uint32_t overrun_errors;
    uint32_t breaks;
    uint32_t transmit_timeouts;
    uint32_t event_sequence;
} scif_stats_t;

static scif_stats_t stats;

static bool byte_access_available(void) {
    return serial_initialized && serial_enabled && !spi_active &&
           dcload_type != DCLOAD_TYPE_SER;
}

static void rb_reset(void) {
    rb_head = 0;
    rb_tail = 0;
    rb_count = 0;
    polled_peek_valid = false;
}

static void record_event(uint32_t *counter) {
    ++*counter;
    ++stats.event_sequence;
}

static void rb_push_char(uint8_t value) {
    if(rb_count == SCIF_RX_BUFFER_SIZE) {
        record_event(&stats.receive_dropped);
        return;
    }

    recvbuf[rb_head] = value;
    rb_head = (rb_head + 1u) % SCIF_RX_BUFFER_SIZE;
    ++rb_count;
}

static int rb_pop_char(void) {
    int value = recvbuf[rb_tail];

    rb_tail = (rb_tail + 1u) % SCIF_RX_BUFFER_SIZE;
    --rb_count;
    return value;
}

static void clear_receive_flags(uint16_t status) {
    /*
       These flags are cleared only after first being observed as set. Preserve
       unrelated state because TEND/TDFE and error details share this register.
    */
    SCFSR2 = status & ~(SCFSR_ER | SCFSR_BRK | SCFSR_RDF | SCFSR_DR);
}

static void drain_receive_fifo(void) {
    /*
       Reading a character updates FER/PER for that exact FIFO entry. Sample
       status after the data read so diagnostics stay associated with the byte
       that is still delivered to the application.
    */
    while((SCFDR2 & 0x1fu) != 0) {
        uint8_t value = SCFRDR2;
        uint16_t status = SCFSR2;

        /* FER/PER describe the character most recently read from SCFRDR2. */
        if(status & SCFSR_FER)
            record_event(&stats.framing_errors);
        if(status & SCFSR_PER)
            record_event(&stats.parity_errors);

        rb_push_char(value);
    }

    clear_receive_flags(SCFSR2);
}

static void scif_err_irq(irq_t src, irq_context_t *context, void *data) {
    uint16_t status = SCFSR2;
    uint16_t line_status = SCLSR2;

    (void)src;
    (void)context;
    (void)data;

    if(status & SCFSR_BRK)
        record_event(&stats.breaks);
    if(line_status & SCLSR_ORER)
        record_event(&stats.overrun_errors);

    /*
       Do not log from this IRQ: the selected debug handler may itself be SCIF.
       Preserve readable characters instead of resetting both FIFOs as the old
       handler did.
    */
    drain_receive_fifo();
    clear_receive_flags(SCFSR2);

    if(line_status & SCLSR_ORER)
        SCLSR2 = line_status & ~SCLSR_ORER;
}

static void scif_data_irq(irq_t src, irq_context_t *context, void *data) {
    (void)src;
    (void)context;
    (void)data;

    drain_receive_fifo();
}

static void irq_mode_apply(bool enabled) {
    uint16_t control = SCSCR2;

    /*
       Mask SCIF interrupt generation before replacing handlers or changing
       INTC priority. This prevents an old handler from observing new mode
       state, and the caller already excludes CPU interrupts around the whole
       transition.
    */
    control &= ~(SCSCR_RIE | SCSCR_REIE | SCSCR_TIE);
    SCSCR2 = control;

    if(enabled) {
        irq_set_handler(EXC_SCIF_ERI, scif_err_irq, NULL);
        irq_set_handler(EXC_SCIF_BRI, scif_err_irq, NULL);
        irq_set_handler(EXC_SCIF_RXI, scif_data_irq, NULL);
        irq_set_priority(IRQ_SRC_SCIF, 14);
        SCSCR2 = control | SCSCR_RIE | SCSCR_REIE;
    }
    else {
        irq_set_priority(IRQ_SRC_SCIF, IRQ_PRIO_MASKED);
        irq_set_handler(EXC_SCIF_ERI, NULL, NULL);
        irq_set_handler(EXC_SCIF_BRI, NULL, NULL);
        irq_set_handler(EXC_SCIF_RXI, NULL, NULL);
    }

    scif_irq_usage = enabled;
    rb_reset();
}

static void wait_one_bit(uint32_t actual_baud) {
    uint32_t iterations = actual_baud ? 200000000u / actual_baud : 800000u;

    if(iterations < 32u)
        iterations = 32u;

    /*
       SCBRR changes require at least one complete serial bit interval before
       transfer is enabled. A simple early-boot-safe loop avoids depending on
       timers, threads, or an initialized scheduler.
    */
    while(iterations--)
        __asm__ volatile("nop");
}

static int apply_configuration(const scif_config_t *config,
                               const scif_register_config_t *registers) {
    irq_mask_t irq_state = irq_disable();
    uint16_t interrupt_control = scif_irq_usage ?
                                 SCSCR_RIE | SCSCR_REIE : 0;

    if(spi_active) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    /*
       The hardware initialization order is load-bearing: disable transfer,
       hold both FIFOs reset, select the clock and frame format, wait one bit,
       release the FIFOs, and only then enable transmission and reception.
    */
    SCSCR2 = registers->control_clock;
    SCFCR2 = registers->fifo | SCFCR_TFRST | SCFCR_RFRST;
    SCSMR2 = registers->mode;
    SCBRR2 = registers->bit_rate;
    SCSPTR2 = 0;

    (void)SCFSR2;
    SCFSR2 &= ~(SCFSR_ER | SCFSR_BRK | SCFSR_RDF | SCFSR_DR);
    (void)SCLSR2;
    SCLSR2 &= ~SCLSR_ORER;

    rb_reset();
    wait_one_bit(registers->actual_baud);

    SCFCR2 = registers->fifo;
    SCSCR2 = registers->control_clock | SCSCR_TE | SCSCR_RE |
             interrupt_control;

    serial_config = *config;
    active_config = *config;
    register_config = *registers;
    serial_enabled = true;
    serial_initialized = true;
    irq_restore(irq_state);
    return 0;
}

void scif_set_parameters(int baud, int fifo) {
    scif_config_t config = serial_config;
    scif_register_config_t registers;
    int saved_errno = errno;

    if(baud < 0)
        return;

    config.baud = (uint32_t)baud;
    config.data_bits = 8;
    config.stop_bits = 1;
    config.parity = SCIF_PARITY_NONE;
    config.flow_control = SCIF_FLOW_NONE;

    /*
       The historical void API cannot report invalid input. Validate it
       without changing errno and retain the last valid configuration when
       validation fails.
    */
    if(scif_config_encode(&config, &registers) == 0) {
        irq_mask_t irq_state = irq_disable();

        serial_config = config;
        serial_fifo = fifo != 0;
        irq_restore(irq_state);
    }

    errno = saved_errno;
}

int scif_configure(const scif_config_t *config) {
    scif_register_config_t registers;

    if(scif_config_encode(config, &registers) < 0)
        return -1;
    if(dcload_type == DCLOAD_TYPE_SER || spi_active) {
        errno = EBUSY;
        return -1;
    }

    serial_fifo = true;
    return apply_configuration(config, &registers);
}

int scif_get_status(scif_status_t *status) {
    irq_mask_t irq_state;

    if(!status) {
        errno = EINVAL;
        return -1;
    }

    /*
       One interrupt-excluded snapshot keeps ring indices, IRQ state, hardware
       FIFO counts, and cumulative event counters from describing different
       instants.
    */
    irq_state = irq_disable();
    status->config = active_config;
    status->actual_baud = register_config.actual_baud;
    status->baud_error_ppm = register_config.baud_error_ppm;
    if(spi_active || dcload_type == DCLOAD_TYPE_SER || !serial_initialized) {
        status->receive_queued = 0;
        status->transmit_queued = 0;
    }
    else {
        status->receive_queued = scif_irq_usage ? rb_count :
                                 (SCFDR2 & 0x1fu) + polled_peek_valid;
        status->transmit_queued = (SCFDR2 >> 8) & 0x1fu;
    }
    status->receive_dropped = stats.receive_dropped;
    status->framing_errors = stats.framing_errors;
    status->parity_errors = stats.parity_errors;
    status->overrun_errors = stats.overrun_errors;
    status->breaks = stats.breaks;
    status->transmit_timeouts = stats.transmit_timeouts;
    status->event_sequence = stats.event_sequence;
    status->irq_enabled = scif_irq_usage;
    status->enabled = byte_access_available();
    irq_restore(irq_state);
    return 0;
}

void scif_clear_stats(void) {
    irq_mask_t irq_state = irq_disable();

    stats = (scif_stats_t){ 0 };
    irq_restore(irq_state);
}

int scif_set_irq_usage(int on) {
    irq_mask_t irq_state;

    if(dcload_type == DCLOAD_TYPE_SER || spi_active) {
        errno = EBUSY;
        return -1;
    }

    irq_state = irq_disable();
    irq_mode_apply(on != 0);
    irq_restore(irq_state);
    return 0;
}

int scif_detected(void) {
    return 1;
}

/* The architecture initializes SCIF before selecting a dbgio handler. */
int scif_init_fake(void) {
    return byte_access_available() ? 0 : scif_init();
}

int scif_init(void) {
    scif_register_config_t registers;

    if(dcload_type == DCLOAD_TYPE_SER)
        return 0;
    if(spi_active) {
        errno = EBUSY;
        return -1;
    }
    if(scif_config_encode(&serial_config, &registers) < 0)
        return -1;

    return apply_configuration(&serial_config, &registers);
}

int scif_shutdown(void) {
    irq_mask_t irq_state = irq_disable();

    if(dcload_type == DCLOAD_TYPE_SER) {
        irq_restore(irq_state);
        return 0;
    }
    if(spi_active) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    irq_mode_apply(false);
    SCSCR2 = register_config.control_clock;
    serial_enabled = false;
    serial_initialized = false;
    rb_reset();
    irq_restore(irq_state);
    return 0;
}

static void record_polled_receive_status(uint16_t status,
                                         uint16_t line_status) {
    irq_mask_t irq_state = irq_disable();

    if(status & SCFSR_FER)
        record_event(&stats.framing_errors);
    if(status & SCFSR_PER)
        record_event(&stats.parity_errors);
    if(status & SCFSR_BRK)
        record_event(&stats.breaks);
    if(line_status & SCLSR_ORER)
        record_event(&stats.overrun_errors);

    irq_restore(irq_state);
}

int scif_read(void) {
    irq_mask_t irq_state = irq_disable();

    if(!byte_access_available()) {
        int error = spi_active || dcload_type == DCLOAD_TYPE_SER ? EBUSY : EIO;

        irq_restore(irq_state);
        errno = error;
        return -1;
    }
    if(scif_irq_usage) {
        int value;

        if(rb_count == 0) {
            irq_restore(irq_state);
            errno = EAGAIN;
            return -1;
        }

        value = rb_pop_char();
        irq_restore(irq_state);
        return value;
    }
    else {
        uint16_t status;
        uint16_t line_status;
        int value;

        /*
           Polled peek consumes one hardware FIFO entry into this lookahead
           slot. Reading the slot first preserves application-visible order.
        */
        if(polled_peek_valid) {
            polled_peek_valid = false;
            value = polled_peek_value;
            irq_restore(irq_state);
            return value;
        }

        if((SCFDR2 & 0x1fu) == 0) {
            irq_restore(irq_state);
            errno = EAGAIN;
            return -1;
        }

        value = SCFRDR2;
        status = SCFSR2;
        line_status = SCLSR2;
        record_polled_receive_status(status, line_status);
        clear_receive_flags(status);
        if(line_status & SCLSR_ORER)
            SCLSR2 = line_status & ~SCLSR_ORER;
        irq_restore(irq_state);
        return value;
    }
}

int scif_peek(void) {
    irq_mask_t irq_state = irq_disable();
    int value;

    if(!byte_access_available()) {
        int error = spi_active || dcload_type == DCLOAD_TYPE_SER ? EBUSY : EIO;

        irq_restore(irq_state);
        errno = error;
        return -1;
    }
    if(scif_irq_usage) {
        if(rb_count == 0) {
            irq_restore(irq_state);
            errno = EAGAIN;
            return -1;
        }

        value = recvbuf[rb_tail];
    }
    else {
        uint16_t status;
        uint16_t line_status;

        if(polled_peek_valid)
            value = polled_peek_value;
        else {
            if((SCFDR2 & 0x1fu) == 0) {
                irq_restore(irq_state);
                errno = EAGAIN;
                return -1;
            }

            value = SCFRDR2;
            status = SCFSR2;
            line_status = SCLSR2;
            record_polled_receive_status(status, line_status);
            clear_receive_flags(status);
            if(line_status & SCLSR_ORER)
                SCLSR2 = line_status & ~SCLSR_ORER;
            polled_peek_value = (uint8_t)value;
            polled_peek_valid = true;
        }
    }

    irq_restore(irq_state);
    return value;
}

int scif_read_available(void) {
    irq_mask_t irq_state = irq_disable();
    int available;

    if(!byte_access_available()) {
        int error = spi_active || dcload_type == DCLOAD_TYPE_SER ? EBUSY : EIO;

        irq_restore(irq_state);
        errno = error;
        return -1;
    }

    available = (int)(scif_irq_usage ? rb_count :
                      (SCFDR2 & 0x1fu) + polled_peek_valid);
    irq_restore(irq_state);
    return available;
}

int scif_read_buffer_nonblock(uint8_t *data, size_t len) {
    size_t read = 0;

    if((!data && len != 0) || len > INT_MAX) {
        errno = len > INT_MAX ? EOVERFLOW : EINVAL;
        return -1;
    }

    while(read < len) {
        int value = scif_read();

        if(value < 0) {
            if(errno == EAGAIN)
                break;
            return read ? (int)read : -1;
        }

        data[read++] = (uint8_t)value;
    }

    return (int)read;
}

int scif_write_available(void) {
    irq_mask_t irq_state = irq_disable();
    int queued;

    if(!byte_access_available()) {
        int error = spi_active || dcload_type == DCLOAD_TYPE_SER ? EBUSY : EIO;

        irq_restore(irq_state);
        errno = error;
        return -1;
    }

    queued = (SCFDR2 >> 8) & 0x1f;
    if(queued > (int)SCIF_FIFO_CAPACITY)
        queued = SCIF_FIFO_CAPACITY;
    irq_restore(irq_state);
    return (int)SCIF_FIFO_CAPACITY - queued;
}

int scif_try_write(int c) {
    irq_mask_t irq_state = irq_disable();
    uint16_t status;
    int queued;

    if(!byte_access_available()) {
        int error = spi_active || dcload_type == DCLOAD_TYPE_SER ? EBUSY : EIO;

        irq_restore(irq_state);
        errno = error;
        return -1;
    }

    /*
       TDFE depends on the configured trigger and does not mean the FIFO is
       completely empty. SCFDR2 gives the exact occupancy needed by a true
       nonblocking write.
    */
    queued = (SCFDR2 >> 8) & 0x1f;
    if(queued >= (int)SCIF_FIFO_CAPACITY) {
        irq_restore(irq_state);
        errno = EAGAIN;
        return -1;
    }

    status = SCFSR2;
    SCFTDR2 = (uint8_t)c;
    SCFSR2 = status & ~(SCFSR_TDFE | SCFSR_TEND);
    irq_restore(irq_state);
    return 1;
}

int scif_write(int c) {
    int spins = SCIF_LEGACY_SPINS;

    while(spins-- > 0) {
        if(scif_try_write(c) == 1)
            return 1;
        if(errno != EAGAIN)
            return -1;
    }

    {
        irq_mask_t irq_state = irq_disable();
        record_event(&stats.transmit_timeouts);
        irq_restore(irq_state);
    }

    errno = ETIMEDOUT;
    return -1;
}

int scif_flush(void) {
    int spins = SCIF_LEGACY_SPINS;

    while(spins-- > 0) {
        irq_mask_t irq_state = irq_disable();

        if(!byte_access_available()) {
            int error = spi_active || dcload_type == DCLOAD_TYPE_SER ?
                        EBUSY : EIO;

            irq_restore(irq_state);
            errno = error;
            return -1;
        }
        /* TEND means both the FIFO and the shift register have drained. */
        if(SCFSR2 & SCFSR_TEND) {
            irq_restore(irq_state);
            return 0;
        }

        irq_restore(irq_state);
    }

    if(spins <= 0) {
        irq_mask_t irq_state = irq_disable();
        record_event(&stats.transmit_timeouts);
        irq_restore(irq_state);
        errno = ETIMEDOUT;
        return -1;
    }

    return 0;
}

int scif_write_buffer(const uint8_t *data, int len, int translate) {
    int written = 0;

    if((!data && len != 0) || len < 0) {
        errno = EINVAL;
        return -1;
    }

    while(len-- > 0) {
        int value = *data++;

        if(translate && value == '\n') {
            if(scif_write('\r') < 0)
                return -1;
            ++written;
        }

        if(scif_write(value) < 0)
            return -1;
        ++written;
    }

    if(serial_fifo && scif_flush() < 0)
        return -1;
    return written;
}

int scif_read_buffer(uint8_t *data, int len) {
    int read = 0;

    if((!data && len != 0) || len < 0) {
        errno = EINVAL;
        return -1;
    }

    while(len-- > 0) {
        int value;

        while((value = scif_read()) < 0) {
            if(errno != EAGAIN)
                return -1;
        }

        *data++ = (uint8_t)value;
        ++read;
    }

    return read;
}

int _scif_spi_claim(void) {
    irq_mask_t irq_state;

    if(dcload_type == DCLOAD_TYPE_SER) {
        errno = EBUSY;
        return -1;
    }

    irq_state = irq_disable();
    if(spi_active) {
        irq_restore(irq_state);
        errno = EBUSY;
        return -1;
    }

    /*
       SPI changes every register in this bank. Disable and remember byte-mode
       receive IRQs before publishing exclusive ownership, all without a
       preemption window.
    */
    spi_saved_irq_usage = scif_irq_usage;
    irq_mode_apply(false);
    spi_active = true;
    serial_enabled = false;
    irq_restore(irq_state);
    return 0;
}

int _scif_spi_release(void) {
    int result;
    bool restore_irq;
    irq_mask_t irq_state = irq_disable();

    if(!spi_active) {
        irq_restore(irq_state);
        errno = EINVAL;
        return -1;
    }

    restore_irq = spi_saved_irq_usage;
    spi_active = false;
    spi_saved_irq_usage = false;

    /*
       Keep the transition non-preemptible until the retained byte format and
       prior IRQ mode are restored. A competing configure call can therefore
       observe either SPI ownership or a completely usable byte port, never a
       half-restored register set.
    */
    result = scif_init();
    if(result == 0 && restore_irq)
        irq_mode_apply(true);

    irq_restore(irq_state);
    return result;
}

dbgio_handler_t dbgio_scif = {
    .name = "scif",
    .detected = scif_detected,
    .init = scif_init_fake,
    .shutdown = scif_shutdown,
    .set_irq_usage = scif_set_irq_usage,
    .flush = scif_flush,
    .write_buffer = scif_write_buffer,
    .read_buffer = scif_read_buffer
};
