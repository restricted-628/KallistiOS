# Private native sequence authoring

ASQ is an original draft container for the private AICA sequence engine. It
contains no samples and has no Sega bank/ABI compatibility. This utility prepares
native programs; it does not upload them, publish an ARM bank, reserve voices,
start hardware or change the default firmware.

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
