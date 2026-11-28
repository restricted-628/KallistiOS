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
after the index or immediately before the first data extent. If both
locations are available, the inline pointer takes precedence. Unnamed
archives use a zero-size pointer or omit it when there is no pointer gap.
Table sizes and ranges are checked using byte-safe loads and wide offsets.
The member's extent always comes from the ordinal table.

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
afs repack OLD.AFS NEW.AFS
```

Packs use 2048-byte alignment and the inline pointer. Named packs store
each input's basename, zero timestamp fields, and its byte size in the
metadata record. Repacking preserves ordinal IDs, zero holes, payloads,
names and their 16 metadata bytes while writing the canonical layout.
Extraction writes numeric filenames into a new directory, independently
of the stored names. Output commands require fresh paths.
