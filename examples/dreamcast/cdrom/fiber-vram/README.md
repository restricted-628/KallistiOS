# Direct disc-to-VRAM DMA with application fibers

This example reads a prepared 128 x 128 linear RGB565 texture (32 KiB, sixteen
cooked sectors) straight from the optical drive into allocated texture RAM.
It uses `libfiber_disc` with two application-owned fibers, not a service
executor. A sibling keeps doing bounded work while the loader is parked.
The existing disc worker executes the I/O; no new thread or payload staging
buffer is allocated by the example.

## Build and media

Build/install `addons/libfiber_disc`, then run `make` here in your normal KOS
environment. The host C compiler (`HOST_CC`, default `cc`) builds a small
generator that creates `texture.bin`. Put this file at the root of your test
ISO as `/texture.bin`, alongside the executable packaged with your usual
homebrew boot-image tooling. Use a real mounted data image: launching the ELF
with an empty virtual drive is not a successful test.

The texture is headerless little-endian RGB565, row-major, not twiddled,
compressed, paletted or mipmapped. It has red/green/blue/yellow quadrants and
white borders/diagonals. It must occupy one contiguous, non-interleaved extent,
with no extended-attribute blocks. The example discovers its FAD through `/cd`
metadata before dispatching fibers, checks this layout and the exact file size,
then uses cooked Mode-1 or Mode-2 Form-1 reads. Keep the same disc inserted.
No ISO filename, file-header parser or texture conversion runs inside the DMA
engine. The default filesystem and payload paths are direct, not BIOS.

## Ownership sequence

1. Initialize PVR and allocate the texture plus two 32-byte guard regions using
   `pvr_mem_malloc`. Keep its texture-memory aperture; do not substitute the
   other VRAM aperture, which has a different byte layout.
2. Submit `fiber_disc_read_dma` into the aligned interior of the allocation.
   No rendering, uploads, or other DMA may touch this allocation yet. This is
   the ordinary one-shot read API, not the RAM-only staged stream API.
3. The main fiber pumps completion and dispatches ready fibers. The loader's
   `fiber_disc_await` parks only that fiber. Do not replace it with
   `cdrom_request_wait`, which would block the owning OS thread.
4. Await returns only after the underlying request and callback retire. Check
   the terminal state and byte count; destroy the read and adapter. Verify all
   texture words against the generated pattern and check both guards.
5. Only a successful, verified read is rendered. The example submits 180 frames
   and waits for `pvr_wait_render_done` before freeing the texture. A final
   `pvr_wait_ready` alone is not a last-frame texture-lifetime fence.

The main loop has a 30-second application deadline (including queue residence)
in addition to the read's 10-second execution deadline. On application timeout,
it requests shutdown/cancellation and continues pumping and dispatching for a
bounded drain. Cancellation is not permission to release VRAM. If retirement
cannot be established, it panics without freeing live resources. Failed or
partial reads are never rendered. Uncached VRAM does not need data-cache
flushes; the driver handles the transport, not a blanket memory clobber.

## Validation gates

`FIBER-VRAM: bytes/guards PASS` confirms the transfer contents; final
`FIBER-VRAM: PASS` additionally requires rendering completion and no recorded
PVR pipeline fault. Check the visible quadrant pattern too: completion alone
does not prove correct texture sampling or display output.

The no-media `fiber-disc-contract` probe separately tests cancellation,
timeouts, callback retirement and sibling scheduling with simulated transport.
Those tests do not prove VRAM bus behavior. Live-media emulator execution and
physical Dreamcast execution are separate gates; physical hardware testing is
still required. This is a correctness/lifetime example, not a throughput claim.

On 2026-09-23, SH-4 GCC 16.2 built/linked the example with `-Werror`.
Apple Clang (including ASan/UBSan) and GCC 14 generated identical texture bytes.
A generated Mode-1 CUE/ISO boot image passed the full 32 KiB readback, guards,
and render-completion checks in Flycast interpreter and dynarec modes.
Corrupted-pixel and missing-file images were rejected without rendering in
both modes. No framebuffer capture or physical hardware validation was
performed.
