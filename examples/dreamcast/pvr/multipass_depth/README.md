# Multipass depth boundaries

Build with a sourced KOS environment and `make`, then load
`multipass-depth.elf`. No assets or optional ports are needed. The example
uses 12 KiB of caller-owned staging only to exercise the buffered modes.

This runs nine cases: direct, DMA and hybrid submission, each with the legacy
API, explicit preserve, and a depth clear before pass one. Each case renders
twelve identical frames, waits for completion, checks pipeline faults, and
reads five pixels from the displayed RGB565 framebuffer:

- Blue background outside every panel.
- Red pass-zero color outside the second panel, retained even across a clear.
- Red at the center when depth is preserved; green there after a clear.
- Cyan where pass-two geometry is nearer than both earlier passes.
- Red near the outer panel's opposite corner.

A farther yellow panel in pass two must remain invisible. It catches a clear
that incorrectly leaks into later preserve boundaries. The first two cases
must produce identical pixels. Policy storage is deliberately changed after
initialization to verify that the driver copied it. Invalid policies and a
null array must leave a VRAM sentinel unchanged. Checks are active even with
`NDEBUG`; submission is not hidden inside assertions.

The test expects 640x480 RGB565 and an emulator that writes rendered pixels
back into emulated VRAM. For Flycast enable `rend.EmulateFramebuffer=yes` as
well as the serial console; without it, numeric reads may see a stale black
framebuffer even when the window displays rendered geometry. Run interpreter
and dynarec separately, with only one emulator instance open.

The fixture also checks all 900 submitted tile control words per case. It
continues after pixel mismatches to report the complete matrix, but ends with
failure unless all 45 samples match. Current Flycast Vulkan runs produce
three center-pixel mismatches (one per clear case) despite correct region
controls. OpenGL 4.1 dynarec reproduces them; see the
[recorded limitation](../../../../doc/pvr-multipass-design.md#depth-boundary-validation).

A PASS validates this fixture, not physical-console depth precision, overflow
timing, modifier interaction, or all rendering backends. This is not yet a
portal renderer: the clear affects every tile; arbitrary portal coverage and
occlusion constraints still belong to the application.
