# Private host music command client

This is the host state machine for the private resident music wire candidate.
It assigns no absolute RAM address, advertises no firmware capability, selects
no default build, and performs no G2/IRQ/cache operation itself. Its callbacks
are the integration boundary for the eventual ordered SH-4 transport.

Initialize once with a nonzero established sound session and serialized callbacks.
Poll SYNC until a pristine ready receipt is observed. Existing claimed state is
never adopted. Begin a validated operation from IDLE; the client constructs the
16-word packet and nonwrapping cookie. Submit must copy the entire packet before
returning.1 means admitted,0 means not admitted on that attempt,negative means
admission may have happened. Every attempted operation remains WAIT, including a
known failed submission. Retry sends identical bytes and cookie. There is no
implicit timeout, cancellation, abandon, allocator or scheduler.

Poll makes at most three receipt reads: sequence,64-word body,sequence, with sound
session checks before and after. Odd/changing sequences and transient read errors
remain pending. A coherent frame must match version/session, exact cookie/op and
all request words. Reserved fields, ranges, logical voice totals, generations,
operation-specific success state and cancellation identity are checked. Malformed,
foreign/new-session or exhausted state latches LOST and keeps request evidence.
A prior receipt remains pending; a pending control is not completion. The actual
G2 read backend must supply ordering and native word values. Host tests cannot
prove that backend or physical sound-RAM visibility.

A terminal receipt yields DONE, even for a valid negative operation result.
The caller inspects `receipt.result`;1 from poll means terminal, not successful
operation. `aica_music_client_consume` then returns to IDLE. The receipt remains a
snapshot, not continuously refreshed status. A fresh QUERY/cookie is required to
observe subsequent engine progress. The `uncertain` flag records transport history
for the transaction; DONE supplies the exact resolution despite that history.

Urgent quiesce can supersede WAIT. The entire prior packet stays in `superseded`
until DONE is consumed, allowing its owner to reconcile queued references. Repeated
quiesce calls retry the same packet. It closes local admission even when the enqueue
is uncertain. An OPEN that executed without its ACK being observed may have advanced
a port generation before quiesce; the client keeps that generation unknown until a
later exact receipt/query. No stale generation is invented. After quiesce, only
QUERY or another quiesce may be submitted. Never reinitialize to evade LOST.

**This module acquires and frees no resources.** The enclosing serialized owner
must reserve the physical pool and acquire queued bank references before submitting
commands, hold them through WAIT/uncertain/LOST, and reconcile exact terminal
receipts before releasing transient holds. Keep client storage alive until no
transport can reference it. The actual bank manager/host wrapper remains an
integration task. MESSAGE completion is FIFO admission, not voice execution.
Close/quiesce completion does not prove sample-fetch retirement or permit physical
voice, sample allocation or pool reuse. Runtime/reset ownership remains external.

`make test` / `make sanitize` connect this actual client to the actual resident
wire/MIDI code with modeled read/submit callbacks. They cover uncertain admitted
and nonadmitted submissions, identical retries, all read-failure stages, torn
receipts, malformed receipt retention, queued controls, urgent cancellation,
unobserved OPEN completion, all eight port generations, fades/mailboxes and cookie
exhaustion. No live firmware, emulator, audible output or hardware proof is claimed.
