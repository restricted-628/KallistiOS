# Checked framebuffer-surface queries

This example describes the hardware-displayed surface, KOS's current CPU
drawing surface, and configured framebuffer slot zero. It validates geometry,
VRAM bounds, selectors, resolved indices, and the distinction between visible
byte size and known slot capacity.

The example briefly redirects only the CPU convenience pointer to slot zero to
verify slot resolution, then restores its original target. It subsequently
initializes PVR, which clears VRAM, and proves that PVR-managed display and draw
addresses do not acquire a fabricated configured-slot capacity. It does not
submit a scene or change framebuffer pixels after initialization.

Build with `make`, or use `make run` with the configured KOS loader.
