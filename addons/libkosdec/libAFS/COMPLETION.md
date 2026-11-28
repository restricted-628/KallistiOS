# libAFS completion inventory

libAFS implements nine archive runtime behaviors and seven host authoring workflows.

## Runtime (9/9)

| Archive operation | Native coverage |
| --- | --- |
| `ADXF_LoadPartition` | Load a resident ordinal table and bind it to one of 256 partition IDs. |
| `ADXF_LoadPartitionEx` | Load a source opened in a caller-selected directory and retain its directory token. |
| `ADXF_LoadPartitionNw` | Begin without I/O, then service bounded reads cooperatively. |
| `ADXF_AddPartition` | Attach a sparse overlay, resolve newer entries first and fall back through holes. |
| `ADXF_GetPtStat` | Observe a load's progress, wait, success or sticky failure. |
| `ADXF_GetPtinfoSize` | Report the active resident range-array footprint and size it before load. |
| `ADXF_OpenAfs` | Open a member by partition ID and ordinal and read from the selected source. |
| `ADXF_GetFnameRange` | Return the selected archive's filename and sector range. |
| `ADXF_GetFnameRangeEx` | Return the same range with the selected archive's directory token. |

Directory lookup and transport scheduling belong to the caller. Sector
queries cover 2048-byte-aligned extents and round the sector length up.
Byte reads stop at the declared member length. Work sizing and load
statuses follow the native types. The standalone reader also exposes
optional embedded names and metadata. Generic ADXF file-service calls,
ADX playback and binary compatibility are outside this inventory.

## Host authoring (7/7)

| Authoring workflow | Native coverage |
| --- | --- |
| Ordered ALS input and default naming | `pack-list` assigns ordinals by line order and derives the output stem. |
| Source directory | `-dir` supplies the source root. |
| Alignment and layout | Default 2048-byte alignment, configurable sector multiples, fixed 512 KiB header and name pointer. |
| File-ID header and prefix | Combined ordinal C macros with optional `-hd` prefix. |
| ALG log | Output path, start and padded byte length. |
| Sparse overlay writer | Preserve base IDs, update matching names and append new names. |
| Member metadata | Basename, local source mtime at two-second resolution and exact byte count. |

The tool uses fresh output paths, zero-filled padding and printable
ASCII list paths resolved on the host filesystem. For replacement
overlays it writes the full visible count so all generated IDs are
reachable.

## Qualification

Optimized host and ASan/UBSan suites exercise reader bounds, resident
load, wait/error transitions, overlay ownership, output collisions,
and the authoring layout and payloads. The default and append-only
fixtures matched the reference archives byte for byte. The 6144-byte alignment fixture
matched the defined fields and payload; native padding is zero.

A SH-4 guest on mounted ISO9660 passed in Dreamcast BIOS Flycast
interpreter and dynarec. It checked a 6144-byte-aligned base archive,
a native sparse replacement/addition overlay, deferred loading, both
directory identities, sector rounding, exact payloads and member
lifetime after detach. Retail hardware and stock KOS execution remain
unverified.
