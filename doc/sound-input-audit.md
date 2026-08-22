# Dreamcast sound-input capability audit

## Baseline

KOS already had a Maple microphone driver with 8 kHz and 11.025 kHz capture,
16-bit linear and 8-bit companded samples, adjustable raw gain, and an
IRQ-context packet callback. That transport was functional but placed all
retention and overrun handling on the application.

The previous control path also had two correctness problems. A non-successful
device response did not wake a blocking caller, and a timeout could force a
still-queued Maple frame to the vacant state. The first could report success
after a rejected command; the second allowed the same frame storage to be
reused while the bus still owned it.

## Native KOS additions

The existing callback API remains available. New capture objects attach one
caller-owned ring buffer to a microphone, and independent stream objects retain
their own 64-bit read positions. Readers can begin at the oldest retained
sample or at the live edge, perform nonblocking reads, seek within retained
data, and inspect sticky overrun and exact lost-sample accounting.

Ring storage is byte-oriented internally and sample-oriented publicly.
Sixteen-bit capture rejects partial trailing samples so write, read, loss, and
seek positions remain aligned. The IRQ writer never traverses reader lists.
Each reader detects overwrite by comparing its absolute position with the
oldest position still representable by the ring.

Large application reads do not mask interrupts for their full length. Copies
are split into at most 256-byte interrupt-excluded pieces, and overrun is
re-evaluated between pieces. This prevents torn reads while bounding Maple and
scheduler latency.

## Control and status

Start and stop now use one response-aware state machine. Every response records
its raw code, publishes the final state, releases the Maple frame, and wakes
waiters in that order. A timeout reports `MAPLE_ETIMEOUT` without changing
frame ownership; a later response still completes normally.

`sip_get_status()` returns a coherent device snapshot with configuration,
state, response/result, sequence, packet/sample totals, malformed-response
count, command failures, timeouts, and the last raw sample-status word.
Capture and stream snapshots expose retained capacity and reader-specific loss
without requiring direct access to interrupt-mutated driver fields.

## Resource proportionality

Normal Maple initialization allocates no sample ring, reader, worker thread, or
periodic service beyond the pre-existing device status and poll callback. A
capture allocates one small control object only when requested. The application
supplies and sizes the sample buffer. Each independent reader allocates one
small cursor object.

## Validation

`utils/sip-stream-test` builds the actual ring implementation as strict C11 and
checks contiguous writes, wraparound, overwrite normalization, exact lost-byte
accounting, oversized packet truncation to the newest capacity, and wrapped
copy-out.

`examples/dreamcast/maple/sip-buffered` exercises the public lifecycle on a
physical microphone without using an interrupt-context application callback.
It records into a 16 KiB caller buffer and reports samples, peak amplitude, and
reader loss.

## Remaining gates

- Physical start, stop, packet cadence, removal, and reattachment.
- Both sample formats and both sample rates on more than one microphone model.
- Meaning of device-specific bits in the raw sample-status word.
- High-gain mode discovery and control on devices which expose it.

The last two items remain unpromised until their descriptor and wire behavior
can be validated without guessing.
