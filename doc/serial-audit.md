# SCIF serial audit

## Scope

This tranche strengthens KOS's byte-oriented SH-4 SCIF driver. It does not
introduce a second serial API family, a compatibility runtime, a background
worker, or an application-owned work-area requirement.

The driver continues to coexist with the debug-I/O layer, the serial loader,
PPP, the debugger transport, and bit-banged SCIF-SPI consumers.

## Capability result

| Capability | Previous KOS behavior | Current KOS behavior |
|---|---|---|
| Line format | 8 data bits, no parity, one stop bit only | Checked 7/8-bit, none/even/odd parity, and one/two stop-bit configuration |
| Bitrate | Threshold-based integer truncation | Nearest valid clock-divider/BRR selection across all four internal clocks, with actual rate and signed error reported |
| External clock | Legacy zero-baud convention | Preserved and represented explicitly in the configuration snapshot |
| Hardware flow control | Disabled, with incorrect software pin manipulation near ring exhaustion | Explicit automatic RTS/CTS selection; false manual-flow behavior removed |
| Nonblocking transmit | Not exposed | scif_try_write() and scif_write_available() |
| Receive readiness | Single-byte scif_read() only | Queue count, nonblocking buffer read, and coherent status |
| Receive peek | Not exposed | Works in IRQ mode and through one-byte lookahead in polled mode |
| Break and receive errors | Printed recursively from IRQ context, then both FIFOs were reset | Counted without IRQ printing; readable bytes are retained and diagnostic counters are snapshotted |
| Software-ring overflow | Overwrote unread bytes and let the count exceed capacity | Drops only the new byte and records the loss |
| Timeout recovery | One timeout permanently disabled SCIF | ETIMEDOUT is reported and counted; later operations and reinitialization remain possible |
| Shutdown | Disabled IRQ use but left byte transfer active | Disables IRQs and byte transfer while retaining configuration for reinitialization |
| SCIF-SPI ownership | Byte IRQ state and SPI register use could silently overlap | SPI claims the register bank, rejects competing owners, and restores the prior byte/IRQ mode on release |
| PPP errors | Write, flush, init, and IRQ-enable failures were ignored | Failures are returned to the PPP layer |
| Debugger bitrate | Selected 57600 without applying it | Reinitializes SCIF after selecting the debugger bitrate |

## Public API additions

- scif_configure() validates and immediately installs a scif_config_t.
- scif_get_status() returns one coherent configuration, FIFO, availability,
  error, break, drop, timeout, and sequence snapshot.
- scif_clear_stats() clears cumulative diagnostics.
- scif_read_available() reports immediately readable bytes.
- scif_read_buffer_nonblock() consumes only currently available bytes.
- scif_peek() observes the next byte without removing it from the application
  stream.
- scif_write_available() reports free transmit-FIFO entries.
- scif_try_write() attempts one bounded, nonblocking FIFO write.

The older scif_set_parameters(), scif_read(), scif_write(),
scif_read_buffer(), scif_write_buffer(), and dbgio entry points remain
available.

## Execution and resource model

The serial improvements allocate no heap memory and create no thread. The
existing 1024-byte receive ring remains the only software payload buffer.
Receive IRQ work is bounded by the 16-byte hardware FIFO. Applications that
use only polled output do not enable the receive IRQ path.

Application callbacks are deliberately not invoked from SCIF interrupts.
Error and break state is published through a monotonically changing status
sequence and cumulative counters. An application that needs deferred policy
can observe that snapshot from its own thread, workqueue, or service fiber.

## Ownership and concurrency

Byte I/O and SCIF-SPI are mutually exclusive uses of one register bank.
SCIF-SPI acquisition disables byte receive IRQs and preserves whether IRQ mode
must be restored. Byte operations return EBUSY while the serial loader or SPI
owns the port. Configuration and FIFO operations use short interrupt-excluded
critical sections so thread preemption cannot split ownership checks from a
register access.

The compatibility scif_set_parameters() function still stores parameters for
the next scif_init(). Its fifo argument now performs the documented practical
choice: nonzero flushes at the end of scif_write_buffer(), while zero avoids
that terminal wait. The hardware FIFO itself is always present.

## Deferred work

SCIF can request SH-4 DMAC service for transmit and receive. Adding that path
requires explicit DMA-channel ownership, cache/alignment contracts, request
lifetime rules, cancellation, and coexistence with the SCI driver. It is not
required for the small command/debug traffic represented by this tranche and
has not been added speculatively.

Raw manual modem-pin overrides are also deferred until cable polarity and
wiring can be physically validated. Normal automatic RTS/CTS operation is
available through scif_config_t.

## Validation boundary

Host tests compile the production configuration encoder and verify framing,
trigger, external-clock, standard high-speed, exact maximum-rate, and invalid
configuration vectors.

The following remain physical-hardware validation gates:

- 7-bit, parity, and two-stop-bit interchange with a host UART;
- automatic RTS/CTS behavior and cable polarity;
- high-rate clock tolerance across representative adapters;
- framing, parity, overrun, and break counter behavior;
- SCIF-SPI takeover and restoration while receive IRQ mode was active.
