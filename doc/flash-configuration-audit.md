# Dreamcast flash configuration audit

## Scope

This audit covers KOS access to the internal flash block store, the system
configuration record, and per-title play history. It deliberately separates
read-only parsing from mutation: inspecting a malformed record must never
trigger an erase, refresh, repair, or append operation.

## Existing KOS baseline

KOS already provided BIOS-backed partition discovery, raw reads, raw writes,
partition erase, logical-block lookup, region lookup, basic language/audio/
autostart decoding, and ISP-setting parsers. The important missing pieces were
not low-level flash access. They were:

- complete bounds validation for block-allocated partition geometry;
- safe scanning when a bitmap contains holes or a zero byte at its tail;
- a payload-only logical-block API that does not expose record metadata;
- strict decoding of all known system-configuration fields;
- title play-history discovery, CRC validation, and endian conversion;
- host-testable decoders that do not require flash hardware.

## Block-store model

A block-allocated partition consists of a 64-byte header, a sequence of
64-byte records, and a bitmap rounded up to whole 64-byte storage units at the
end. Each record contains a two-byte logical identifier, 60 data bytes, and a
two-byte CRC. A cleared bitmap bit marks an allocated record.

`flashrom_get_block()` now validates the header and geometry, limits scanning
to physical data records, scans allocated records newest-first, and ignores
matching records with a bad CRC. This removes the old tail overrun and also
allows a valid newer record to be found after a bitmap hole.

`flashrom_read_block()` publishes only the 60-byte payload and optionally
returns the resolved logical and physical record identifiers.

## System configuration

`flashrom_get_syscfg_ex()` returns the known setting timestamp, language,
audio mode, and disc-autostart state. Unlike the legacy accessor, it rejects
unknown language and boolean encodings rather than converting them into a
plausible setting. `flashrom_syscfg_decode()` provides the same validation for
an already-extracted payload.

The legacy `flashrom_get_syscfg()` remains available and keeps its established
result vocabulary. It now rejects a null output pointer.

## Play history

The game-settings partition can contain 100 title slots. Each slot is four
logical blocks beginning at identifier 24. The public KOS structure combines
the four payloads, terminates fixed-width strings, and converts multi-byte
fields to SH-4 host order. It includes:

- product number and two title strings;
- category and first/previous start times;
- start, load, save, network, and flash-save counts;
- 24 play-time buckets;
- peripheral summary, evaluation, progress, and 32 title-defined bytes.

`flashrom_play_history_read()` addresses a slot directly.
`flashrom_play_history_find()` searches by the fixed-width ten-byte product
number. `flashrom_play_history_decode()` validates and decodes four payloads
without reading hardware. `flashrom_play_history_encode()` performs the inverse
conversion, preserves both reserved regions, and regenerates the title CRC.

The title CRC spans the product number through the first-start timestamp. It
is independent of each logical block's record CRC; both layers must validate
before data is returned.

## Bounded mutation

`flashrom_append_block()` now exposes the block store's append operation
without exposing an automatic erase or refresh. `flashrom_set_syscfg_ex()`
uses that primitive to preserve every unknown payload byte while changing the
known timestamp, language, audio, and autostart fields.

`flashrom_play_history_write()` compares all four desired packets with the
latest stored copies, preflights enough erased records for the complete change,
and appends only packets whose bytes differ. It writes counter packets 2 and 3
first, followed by identity packet 1 and then packet 0. The result structure
reports the requested packet mask, packets whose append and verification
completed, the packet with ambiguous completion, and every verified physical
record.

The four-packet format has no transaction marker. A power loss can publish
only the counters that completed, and an interruption after a changed packet 1
but before packet 0 temporarily leaves the title CRC inconsistent. Retrying the
same desired record appends only the missing packet and repairs the title. A
capacity failure is detected before the first packet is written.

The block store allocates a record by clearing its bitmap bit before writing
the record. That order is intentional. If power fails after allocation, the
CRC-invalid record is ignored and the previous valid value remains visible;
one physical record may be lost. Writing the record first would instead leave
programmed storage marked free, allowing a later append to attempt an
impossible zero-to-one bit transition. The writer therefore uses:

1. validate the partition geometry and find an erased free record;
2. clear and verify its allocation bit;
3. program and verify the identifier and 60-byte payload;
4. program the two-byte CRC last and verify the complete record.

No append operation needs an erase. When no erased free record remains, the
API reports `FLASHROM_ERR_NO_SPACE` rather than refreshing implicitly.
A refresh must erase and reconstruct an entire partition. Because the flash
layout has no second on-chip commit area, that operation cannot preserve the
old partition across every possible power loss and must remain a separate,
explicit maintenance facility.

Raw writes, raw partition erases, logical appends, and system-configuration
updates share one programming mutex. Read-only scans remain lock-free and may
briefly observe an allocated record with an invalid CRC; they safely ignore it
and return the preceding valid value. A write or verification error is an
ambiguous completion: callers must reread the logical block before retrying,
because the final program operation may have completed before reporting an
error.

## Validation

`utils/flashrom-layout-test` constructs independent byte-level fixtures and
checks partition geometry, bitmap allocation, every append interruption point,
little-endian system timestamps, big-endian history counters/times, the
little-endian stored title CRC, fixed-string termination, and corruption
rejection. The interruption fixture proves that a complete record remains
invisible before allocation, every incomplete allocated record falls back to
the previous valid value, and only the complete final CRC publishes the new
value.

The same utility builds the actual `flashrom.c` writer against an in-memory NOR
transport. It verifies bitmap/data/CRC call ordering, partial record and CRC
failures, consumed-slot retry behavior, unknown configuration-byte
preservation, four-packet creation, no-op wear avoidance, one-packet counter
updates, capacity preflight, identity interruption, and repair by retry.

The read-only example was also run under both Flycast dynarec and interpreter
modes. Both runs returned `KOSFLASH cfg=0 history=-1` without an SH-4
exception. The configuration record decoded successfully. The current image's
slot zero contains only a partial packet set, so strict history lookup correctly
returned `FLASHROM_ERR_NOT_FOUND`; the complete four-packet path is covered by
the host fixture.

The writer has not been invoked against the current emulator image or physical
hardware. Physical flash programming remains a separate hardware-only gate.
