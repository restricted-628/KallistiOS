# VMU filesystem safety validation

This example performs full and quick format, deliberate fragmentation,
asynchronous defragmentation with byte-for-byte payload checks, asynchronous
write and copy-on-write replacement, block-level progress and callback
validation, pre-commit cancellation, asynchronous deletion, padded readback,
package-backed and raw VFS writes, metadata validation, and cleanup on the
first attached memory card.

This test formats the entire first card and destroys every file on it. Run it
only with an expendable image or after making a verified backup. Its temporary
names are `KOSVMTXN`, `KOSVFSTST`, `KOSVMCANCEL`, `KOSVMDFRAGA`,
`KOSVMDFRAGB`, `KOSVMDFRAGC`, `KOSVMPASS`, and `KOSVMFAIL`.

On success it leaves `KOSVMPASS` containing the raw marker `KOSVMU!`. This is
an externally inspectable completion signal for emulator runs whose serial
console is unavailable. On failure it attempts to leave `KOSVMFAIL` containing
`KOSFAIL` followed by a one-byte stage number.

Successful completion prints `RESULT: PASS`. The host utility contains the
pure format/defragment planner tests and raw image inspection.
