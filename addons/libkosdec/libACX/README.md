# libACX

libACX reads ACX archives by member number.
Each archive starts with a zero word and a member count, followed by
one absolute byte offset and one byte length per member. All fields
are big-endian 32-bit words. Ordinals start at zero. The archive carries
no filenames. Members may be empty, share an offset, or contain
arbitrary data.

Link the standalone SH-4 archive with `-lACX` and include
`<kosdec/acx.h>`. Provide a byte-positioned callback over an immutable
source, then call `kosdec_acx_open`, `kosdec_acx_get` and
`kosdec_acx_read`. The reader validates the complete index using fixed
scratch storage and clips each read at the member's declared length.
The caller keeps the archive and source alive through the last read.
Apart from the callback, the library requires no allocation or I/O services.

## Host tool

```sh
make sh4
make install PREFIX=/path/to/addons
make host
make test
make sanitize
```

The host binary is `.build/host/acx`.

```sh
acx pack effects.acx bell.adx empty.bin door.adx
acx pack-list effects.fls
acx pack-list effects.fls aligned.acx -al16
acx list effects.acx
acx extract effects.acx extracted
```

Pack order sets member IDs. A file list gives one source path per line.
Relative source paths resolve from the current directory. LF and CRLF
input and Windows-style path separators are supported. With the output
argument omitted, `pack-list` derives the `.acx` path beside the list.
The default alignment is four bytes. `-alBYTES` sets a positive byte
alignment for the first payload after the table, subsequent payloads and
the final file size. Padding is zero. Packing preserves exact payload lengths including zero
and requires a total output size of at most UINT32_MAX bytes.
The packer and extractor require fresh output paths. Extraction gives
each member a numerically ordered `.bin` filename.

## Reading and testing

libACX returns the stored bytes; it does not decode them. Applications
that need ADX playback can continue to use libADX's asynchronous archive
lookup and player without linking libACX.

The host tests check exact offsets, lengths and zero padding at 4-, 6-
and 16-byte alignment, and that an empty member keeps its ordinal.
The mounted SH-4 test reads a packed archive by ordinal and checks the
payload bytes and end-of-member handling.
