# Private native sequence authoring

ASQ is an original draft container for the private AICA sequence engine. It
contains no samples and has no external bank/ABI compatibility. This utility prepares
native programs. The optional private native owner below uploads and publishes
them with matching firmware; the authoring CLI reserves no voices or hardware.

```
python3 asq.py build example.json example.asq
python3 asq.py inspect example.asq
make test
make sanitize
```

Build refuses to overwrite any existing destination. It validates before writing,
fsyncs a temporary file, and publishes it with a same-directory no-clobber hard
link. Directory crash durability is not promised. Inspect emits the equivalent
JSON manifest. Duplicate/unknown keys and noninteger numeric values are rejected.
The CLI limits input JSON to8MiB and binary input to the format maximum.

## Authoring

A manifest has one `programs` array, containing1..128 programs in index order.
Each program has `tpqn` (ticks per quarter note,1..9600), `tempo_us` (microseconds
per quarter,1..60000000), and an `events` array. At most65536 events are allowed
across the whole bank. Every event has an unsigned32-bit `delta` in musical ticks
since the previous event and one of these exact field sets:

| kind | Additional fields | Meaning |
|---|---|---|
| `midi` | `status`, `a`, `b` | Status0x80..0xef and two7-bit bytes; unused bytes remain explicit. |
| `tempo` | `us` | Tempo1..60000000, applying to following deltas. |
| `loop` | `count` | Body executes1..65535 times;0 repeats until loop exit/chain. |
| `loop_end` | none | Ends the innermost loop. |
| `end` | none | Required final event; no implicit note-off is authored. |

Loops nest at most four deep and their bodies must have positive total delta
including the loop-end delta. The builder fills reciprocal indices. Equal-time
MIDI events preserve array order. The sequence backend silences remaining notes
when the program ends; source programs still specify their intended note-offs.
Channel conventions and controller support belong to the MIDI engine, not this
container. No note duration or musical timing is inferred from the JSON file.

## Draft ASQ v1 layout

All integers are little endian. Header64 bytes: magic `KOSASQ\0\0` at0;
version u16=1 at8; header length u16=64 at10; total size u32 at12; program count
at16; total event count at20; directory offset=64 at24; event offset at28;
IEEE CRC32 of bytes64..EOF at32; flags=0 at36; remaining24 bytes zero.
Directory records are32 bytes: event byte offset, event count, tpqn, initial tempo
(all u32), then16 zero bytes. Directory and event arrays are contiguous, in program
order, without gaps or unused events. Each16-byte event is delta/value/auxiliary
(u32 each), followed by kind/status/a/b bytes. Kinds1..5 follow the table above;
value is tempo/count/loop-start index where applicable; loop-start auxiliary is
the matching loop-end index. All unused fields are zero. Final padding to32 bytes
is zero. CRC detects accidental corruption; it provides no authentication.

## Native admission and lifetime

Compile `native/asq.c` together with the actual private `aica_sequence.c`.
`asq_measure` checks header, bounded extents and padding and returns allocation
sizes; it does **not** validate CRC or event grammar. Allocate caller-owned native
`aica_seq_event_t` storage for the returned event count, then call `asq_begin` and
`asq_step` until1 (ready) or-1 (invalid). Each step processes at most512 CRC bytes,
32 decoded events or32 grammar events. Validation calls the actual sequence core.

Source bytes may be unaligned. Native output is decoded with byte-safe loads,
never a serialized-struct cast. The context and output must be correctly aligned;
input, context and used output spans must not overlap. An invalid begin preserves
context and output. A later failure may leave partial output, but `asq_get`
refuses every program until the entire bank passes. Invalid measurement/get leaves
its result unchanged. Caller state must not be forged or modified between calls.

Keep the entire input immutable and alive through every validation and descriptor
lookup. After ready, keep decoded events immutable and alive until all pending,
playing, paused and chained program references retire. `asq_get` returns a ready
validation to pass to `aica_sequence_play`/`chain`; it acquires no reference.
The sequence callbacks must acquire an independent storage reference before the
admission owner releases its reference. Tone/sample bank lifetime is separate.
Source/storage maxima are bounds, not guaranteed free Dreamcast RAM. Source plus
decoded storage can temporarily consume roughly twice the event payload. The
caller chooses resource sizes appropriate to its SH-4 and sound-RAM budgets.

The tests compare an independent wire golden, resealed malformed files, all
truncations, nested loops and maximum counts against Python and actual C. They
exercise bounded validation, aliases, capacity failure, unaligned sources and
native sequence retain/play/pause/continue/stop. Host success is not ARM playback,
DSP execution, audio quality or physical retirement evidence.

The private `native/catalog.h` owner composes ASQ admission with sequence-core
retain/release callbacks. It has eight caller-backed slots and sixteen concurrent
validation leases. Initialize once before use. Admit a source and disjoint native
event storage using the slot's previous generation plus one; service incrementally
until the entire bank is ready. Failed or preparing slots expose no programs.

Acquire a lease, pass its validation to the synchronous sequence play/chain call,
then return that lease at the same address. Use `asq_catalog_retain/release` as the
core's program callbacks (or the corresponding timed-MIDI adapter hooks). A core
reference is independent of the temporary lease, so pause and pending chains keep
storage alive. All operations and callbacks share one serialized, nonreentrant
owner. Lease objects and held source/event arrays must remain unchanged and live.

Drop refuses occupied references and preserves generation on success. It can
cancel preparation or discard failed admission; it never replaces an occupied
slot. Generation/counter exhaustion refuses work. A callback ownership violation
latches a fault and prevents reclamation. There is no forced reset/unload API.
This retires only CPU references to sequence data, not samples, voices or DSP
resources. The catalog itself supplies no transport or device upload.
Release lookup visits at most eight slots and 128 directory entries in the one
matching resource; admission retains the existing ASQ per-step work bounds.

Run `make -C native catalog-test catalog-sanitize` for actual parser/core lifetime
coverage. Target code-link probes still do not establish live ARM execution.

## Private resident publication

`native/owner.h` composes the existing SPU transfer API with the private ARM
catalog. Rebuild the ARM firmware and KOS before using it; initialization requires
the sequence-resource feature. The unchanged prebuilt fallback does not supply
this feature. This is an opt-in private interface, not an external binary ABI.

Use one zeroed, exclusive, serialized ordinary-thread owner for the live sound
runtime. Initialize with a 32-byte-multiple transfer chunk (32..32768) and a
nonzero timeout. Begin an EMPTY/CLOSED slot and pump until READY or an error.
The caller's source remains immutable and alive until the last transfer request
is destroyed. An error can leave allocations or admission pending: preserve the
owner and call close/pump; never discard the context after a failed begin.

READY means that the entire ASQ resource passed ARM validation. Query returns
its generation, program/event counts and CPU references. Close cancels uploads
or drops the exact resident generation, then frees both source and decoded
allocations only after the corresponding transfer/catalog ownership retires.
Only one transaction may be active. Exact retry resolves uncertain submission;
query cannot overtake it. A rejected PREPARE licenses release only of its own
exclusive provisional allocations, never storage described by an occupied slot.

Session loss, contradictory receipts or counter exhaustion retain uncertain
storage. There is no force-reset, force-close or runtime restart path. Do not
race sound reset/shutdown, raw catalog commands or another owner. Catalog drop
tracks CPU sequence references only; it proves nothing about physical sample,
voice or DSP retirement. This publication layer starts no sequence playback.

The private version-1 command is 0x0e; its 256-byte coherent receipt occupies
0x29c00..0x29d00, with resident state reserved at 0x2f000..0x30000. Each firmware
pass services at most one bounded ASQ validation step. PREPARE/QUERY/DROP use
session, cookie, slot and generation identity; superseding DROP preserves a
PREPARE tombstone so replay cannot restart cancelled admission.

Run `make -C native wire-test wire-sanitize client-test client-sanitize
owner-test owner-sanitize` for protocol and mocked host lifetime coverage.
`examples/dreamcast/sound/sequence-resources` supplies the native publication
fixture; its result is separate from host tests, playback and hardware evidence.

## Private sequence playback controls

`native/live-owner.h` connects an existing idle music owner and resource owner
in the same live session. It allocates and frees nothing. Keep all contexts and
their banks/pool alive, including after timeout or LOST. Use one serialized
ordinary-thread caller, and leave the other owners idle while a sequence command
is pending. PLAY/CHAIN require a local READY resource; the ARM rechecks readiness
when applying the command. Receipt of a packet alone acquires no program pin.

BIND requires an open, quiet logical music port: no earlier queued work, active
voices or pending music control. It refuses takeover of direct-MIDI notes. The
binding retains a tone-bank producer pin and excludes direct mutators on its port
and global music reset/close/quiesce; queries remain allowed. UNBIND stops CPU
sequence work and releases the producer pin. If that release fails, the guard and
binding remain faulted; there is no force-unbind. Physical tone/sample references
remain retained independently. Bound ports also prevent raw timer reset.

Controls cover play/priority replacement, pause/continue, chain/loop exit, speed
and fades, beat resolution, release policy, query, stop and all-stop. Paused
position, musical debt and conversion fraction remain held. Existing native speed
fades advance in wall time while paused. Continue restores timed notes through
bounded FIFO batches; success acknowledges admission, not hardware execution.
Rejected/declined PLAY preserves the old clock apart from normal scheduled work.

Each main pass services MIDI and publication first, then urgent sequence stops,
one bounded advance per bound port, and one ordinary control/resume step. Each
port dispatches at most 32 sequence events per pass; overdue debt is retained.
The first serviced port rotates through all eight indexes, so every port gets
first admission at least once per eight passes even with a saturated MIDI queue.
This bounds producer starvation; it does not promise equal event counts or that
overloaded scores meet their musical deadlines.
Successful PLAY starts a new clock epoch; successful CONTINUE excludes paused
time while retaining pre-pause debt/fraction. The 4410 Hz timer conversion carries
integer remainder. A gap too large for the core's elapsed-us input faults without
reinitializing ownership; the caller must sample before a full timer wrap.

Command 0x0f uses a coherent receipt at 0x29d00..0x29e00 and feature 0x200. Pending
snapshots describe admission and remain unchanged until terminal completion;
queries cannot overtake that pending receipt. Exact retries preserve cookies.
An observed pending CONTINUE can be superseded by same-port STOP or STOP_ALL,
preserving the complete cancelled request as a tombstone. All-stop reports faults
after partial logical progress rather than claiming atomic success. None of these
controls authorize physical channel/sample reuse or system shutdown.

Run `make -C native live-test live-sanitize live-owner-test live-owner-sanitize`
for actual core/adapter/catalog/protocol checks and mocked host-owner checks.
The `sequence-live` native example uses original programs and tone data and
retains played tone/pool ownership after CPU program cleanup.
