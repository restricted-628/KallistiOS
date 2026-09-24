# VMU package VFS example

This example writes `/vmu/a1/TESTFILE` with a package header and a 4 KiB
payload. It then reopens the file through the normal VFS interface and verifies
that `fs_total()`, `fs_read()`, `SEEK_END`, and `fs_stat()` all use the exact
payload length rather than the package header or final 512-byte allocation
padding. It also opens the file with `O_META` and confirms that raw access still
reports the larger, block-rounded stored image.

The example overwrites only the file named `TESTFILE`. Replacement is
copy-on-write, so the card must have enough free blocks for the complete new
file while the old version remains allocated. A failed replacement leaves the
old directory entry authoritative.

Insert a memory card in A1 and press Start. Success or the relevant error is
printed to the debug console.
