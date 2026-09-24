# Direct staged streaming

Uses direct disc status/TOC and reads eight sectors of the last data track in
two staged DMA transfers. Mode-1 uses the generic direct-default constructor;
XA/CD-I selects the explicit direct Mode-2 Form-1 constructor. The disc must
have at least eight readable cooked data sectors at that track's start.

Each transfer waits for terminal status and callback completion before
destruction. The session is drained and destroyed before a direct PIO read
compares the payload. No BIOS read or automatic fallback is used. The boot
authorization path remains unchanged.

On failure, requests and session are cancelled and drained. If a bounded
cleanup fails, this diagnostic halts rather than releasing live resources or
reusing the DMA buffer. Real media/emulator execution and physical-drive
timing remain validation gates; compiling this example alone is not an I/O test.

Legacy applications must adapt their lifetime/callback/progress assumptions;
the former global stream names no longer exist. See `../stream-bios` for
deliberately BIOS-backed PIO/DMA streaming and `doc/disc-backend-defaults.md`
for the migration contract.
