# libAFS

libAFS reads Dreamcast-era AFS archives with arbitrary member data.
It uses a caller-supplied byte-positioned reader and fixed parser scratch
space. Link `-lAFS` and include `<kosdec/afs.h>`. The archive and source
context must stay alive until the last member read. Serialize access when
the provider uses a shared seek cursor. The core works over memory, files
or other byte-addressable storage.

`kosdec_afs_open` validates the table and member bounds.
`kosdec_afs_get` selects a stable numeric ID, and `kosdec_afs_find` selects
the first exact optional name. `kosdec_afs_read` clips reads to the member's
extent. Empty entries and aliases retain their numeric IDs. Names are
up to 32 bytes, with a C terminator added in the returned object. The
16 metadata bytes are available unchanged.

The reader supports the `AFS\0` little-endian directory and optional
trailing 48-byte name records. The optional pointer can sit directly
after the index, at the end of a fixed 512 KiB header,
or immediately before the first data extent. The inline pointer takes
precedence. Unnamed
archives use a zero-size pointer or omit it when there is no pointer gap.
Sparse append archives may store a compact name table with one
record per populated slot. The reader maps those records to numeric IDs
in order and leaves holes unnamed. Table sizes and ranges are checked
using byte-safe loads and wide offsets.
The member's extent always comes from the ordinal table.

## Load and resolve a partition

Use a zero-initialized `kosdec_afs_registry` and caller-owned partitions
when looking up members repeatedly. Provide an array of `kosdec_afs_range`
entries with room for the archive's member count. Open the source in the
desired filesystem directory and pass the source and directory token to
`kosdec_afs_partition_load`. Bind the completed partition to an ID. Pass
a null token for the default context.

For cooperative loading, call `kosdec_afs_partition_begin` and service
`kosdec_afs_partition_step` until it returns `KOSDEC_AFS_OK` or a load
error. Each step makes at most one read of up to 512 bytes. A provider
that can finish later may return `KOSDEC_AFS_READ_WAIT` without copying
bytes or retaining the destination. Service that provider before retrying.
`kosdec_afs_partition_status` observes the last result.
`kosdec_afs_partition_info_size` reports the resident range-array size
for the newest partition bound to the ID.

`kosdec_afs_partition_add` attaches a validated overlay. Its count
defines the visible ordinal range, and zero-offset holes fall back to
older layers. `kosdec_afs_partition_open` resolves the member and its
actual source. `kosdec_afs_partition_sector_range` returns that source's
filename and directory token together with its sector start and rounded
length. Sector queries require aligned starts. Member reads retain the
exact byte extent, including explicit empty files.

Pop bindings after use. Keep each partition object, its provider and
the referenced source alive until its last returned member view is
released. A detached member still reads the original source. The
registry and partition operations are caller-serialized. Use the
standalone reader for stored names and metadata.

## Build and author

```sh
make sh4
make install PREFIX=/path/to/addons
make host
make test
make sanitize
```

The host binary is `.build/host/afs`. Pass files in the desired member ID order.

```sh
afs list ASSETS.AFS
afs extract ASSETS.AFS extracted
afs pack ASSETS.AFS scene.bin voice.adx text.dat
afs pack-unnamed ASSETS.AFS scene.bin voice.adx text.dat
afs pack-list BASE.als -dir=assets -al=4096 -hd=ASSET_
afs pack-list BASE.als -dir=assets -add=UPDATE.als -hd=ASSET_ --header=UPDATE.h
afs repack OLD.AFS NEW.AFS
```

Packs use 2048-byte alignment and the inline pointer. Named packs store
each input's basename, zero timestamp fields, and its byte size in the
metadata record. Repacking preserves ordinal IDs, zero holes, payloads,
names and their 16 metadata bytes while writing the canonical layout.
Extraction writes numeric filenames into a new directory, independently
of the stored names. Output commands require fresh paths.

`pack-list` reads ordered relative source paths, one per LF or CRLF line,
with an optional source root. It writes an archive with a
fixed 512 KiB header and configurable data alignment in multiples of
2048 bytes. The output defaults to the list's stem with an `.afs`
extension. It also writes an ordinal C header and an `.alg` line containing
the archive path, start (zero), and padded byte length. `-hd` prefixes the
ID macros. File metadata uses the source's local modification time with
two-second resolution. The host resolves paths using its filesystem's
case rules. The list workflow accepts printable ASCII paths and basenames
of up to 32 bytes.

With `-add`, archive and log paths default to the update list's stem.
The combined ID header defaults to the base list's stem. Use
`--header=PATH` to keep a previously generated header. Untouched base
slots are holes. A matching basename keeps its ID, new names append,
and ambiguous IDs are rejected. The writer records the full visible
count, including when an update only replaces existing IDs.

See [the completion inventory](COMPLETION.md) for the supported workflows and validation limits.
