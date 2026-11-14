# Private resident sequence/MIDI adapter

Run `make test sanitize`. The suite compiles the actual sequence, music, bank
resolver and registry code together. Hardware callbacks are modeled; no sound
output, ARM timing or physical sample retirement is established.

A bound port has one exclusive sequence owner. Binding pins its tone-bank
generation, including while paused. The original program callbacks independently
pin immutable event arrays. Unbind first stops logical sequence work, then drops
the binding pin. Previously played channel/bank leases remain held by the bank
adapter. A failed unpin leaves the binding owned and reports failure.

The global timed-note table has 64 entries. Note-on admission records its channel,
key, velocity and priority; note-off and sustain/controller changes update that
state. Exhaustion deterministically drops a new timed note and increments
`dropped`. It does not stall before a future note-off can free an entry. Physical
voice allocation remains the MIDI engine's separate decision; restoring a timed
note retries that allocation with its saved priority.

Pause urgently cancels queued note events for that port and stops its logical
voices while retaining timed-note records. Controller/program changes remain in
FIFO order and are drained before note restoration. Continue visits at most16
records and admits at most4 notes per callback; sustained notes use a note-on/off
pair (at most8 queue events). It returns pending until restoration is admitted,
so callers must keep servicing the MIDI engine between continue attempts.
The sequence clock remains paused throughout that admission process.

Stop clears timed notes; it never declares sample memory reusable. Other ports'
queued work survives cancellation, including wrapped queues. Generation checks
prevent a stale binding from operating on a newly opened port. Direct foreign
MIDI, reset and close writers are outside the exclusive binding contract.

Tests cover ordinary and sustained-note pause/resume, cancellation during an
incremental bank lookup, controller retention, all64 queue-head positions,
64-note exhaustion/recovery, all8 binding pins, stale generations, and retained
voice references after unbind. Live firmware pool ownership, transport/receipts,
resource authoring/publication and real audio qualification are separate gates.
