# Private music command/receipt candidate

This module is not wired into the running firmware, installed as a public header,
or selected by a default build. It supplies a reviewable command-state core with
caller-provided exclusive channel-pool and tone-bank pin callbacks. No absolute
sound-RAM placement is assigned here. Run `make test sanitize` for actual-C tests.

A request is a checked 16-word native KOS packet, timestamp/misc zero, command
candidate0x0d, nonzero monotonically increasing cookie. Eight payload words are
session, operation, port, generation, and four arguments. The active observation
session is supplied by the firmware owner; a mismatched session is rejected.
Operations: claim pool (two masks), open (bank/generation), close, MIDI message
(status/data/data/priority), mute, GM, release mode, parameter (kind/signed value/
fade milliseconds), reset, query, quiesce, reset-all and close-all.

The 256-byte receipt has a nonwrapping seqlock, complete request echo, cookie,
result, port generation/parameters/mailbox, aggregate MIDI counters, logical
voice counts and a count of voices awaiting parameter propagation. It is a
logical state snapshot, not proof of physical register state or sample retirement.
Host code must read it coherently, check every identity/echo field and retain
ownership after malformed/missing receipts. That host adapter is not in this slice.

Only one request may await completion globally. Byte-identical retries reuse the
receipt without repeating side effects. MIDI success acknowledges FIFO admission,
not voice execution. Ordinary controls wait for earlier MIDI work to drain;
close skips queued notes by invalidating its port generation. Quiesce also cancels
a pending control and records its cookie, closes ports and cancels queued work.
Close-all allows later opens; quiesce does not. A later query uses a fresh cookie.

Opening pins the tone-bank generation; closing drops only that producer pin.
A failed unpin retains its ownership bit and faults further admission. The
physical channel pool and played-bank voice references are never released here.
Receipt sequence exhaustion stops admission without inferring resource safety.
The firmware owner must prevent foreign writers to claimed channels and must
serialize all callbacks without reentry. Pool claim failure must acquire nothing.

Tests cover replay/conflicting cookies, session/packet rejection, ordered controls,
queue backpressure, pending-command cancellation, query freshness fields, eight
ports, close-all/reopen, reset-all, pin failures and sequence-counter exhaustion.
These are model-backed host tests; ARM/SH4 layout builds are separate evidence,
and live transport, embedding, emulator and hardware qualification remain open.
