# Private resident sequence core

Run `make test sanitize`. The actual engine is tested against an independent
absolute-deadline oracle over 128 generated scores with tempo changes, fractional
speed and equal-time events. Other cases cover nested/finite/infinite loops,
loop exit and chaining across different resolutions, incremental validation,
MIDI backpressure, pause/resume failure, reference retention, priority, overflow
and partition-invariant speed fades. These are host tests, not ARM timing or
physical audio evidence.

Programs are immutable caller-owned arrays of native 16-byte events. This is an
internal representation, not a serialized bank format. A caller holds the program
through incremental validation and passes a ready validation to play/chain. The
engine acquires an independent pin before replacing existing state; release
occurs only after that program no longer supplies sequence events. This never
releases a sample bank or proves that a physical voice has stopped fetching.

An event's `delta` is musical ticks since the previous event. Initial tempo is
microseconds per quarter note; resolution is 1–9600 ticks per quarter. Tempo
changes affect subsequent deltas. Event types are:

- MIDI: status/data bytes, value and auxiliary zero.
- Tempo: value is 1–60000000 microseconds per quarter; other fields zero.
- Loop: value is iteration count (zero means infinite), auxiliary is matching
  loop-end index. The begin event's delta runs once, outside the repeated body.
- Loop end: value is matching begin index, auxiliary zero. At most four nested
  loops; each loop body must have positive musical duration.
- End: final event only, other fields zero except delta.

Validation and playback each visit at most 32 events per call. Array order breaks
same-time ties; unspent time carries across passes. A blocked emit retains its
cursor and time debt without duplicating its elapsed ticks. Callbacks must bound
their own work. Stop remains immediate; pause preserves sequence location/debt
and asks the backend to retain timed-note state. Continue waits for the backend's
resume callback to accept those notes. That backend is separate integration work.

Speed is relative: multiplier = 1 + speed/4096, accepted range -4032 through8191.
Fades last 0–32767 milliseconds and use integrated integer linear ramps. Splitting
a fade into multiple service calls preserves its time integral. A caller supplies
actual elapsed microseconds; the core assumes no firmware timer frequency. A
paused sequence does not consume musical time, while its speed fade continues.

Chain acquires the successor before releasing any previous pending successor.
At the next loop boundary it switches directly; without a loop it switches at
End. A null successor requests loop exit, through active nested loops. Position
and beat count reset on each play/chain. Stop keeps the final tick count queryable.
Native cursor and tick/beat queries replace proprietary absolute-address queries.
No vendor parser, wire ABI, assets, or VSync rounding is reproduced.
