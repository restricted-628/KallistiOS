# Video modes and the low-level PVR pipeline

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Graphics**.

## Scope and entry points

Scanout/mode policy, PVR scene/TA/ISP/TSP state, render tickets, DMA submission and reservations.

- `kernel/arch/dreamcast/hardware/video.c`
- `kernel/arch/dreamcast/hardware/video_mode.c`
- `kernel/arch/dreamcast/hardware/video_raster.c`
- `kernel/arch/dreamcast/hardware/biosfont.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_scene.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_dma.c`
- `kernel/arch/dreamcast/hardware/pvr/pvr_events.c`
- `kernel/arch/dreamcast/include/dc/pvr.h`

Relevant existing documentation (dated claims must be rechecked):

- `doc/pvr-capability-audit.md`
- `doc/pvr-render-tickets.md`
- `doc/pvr-vram-reservations.md`
- `doc/video-cable-audit.md`
- `doc/pvr-multipass-design.md`

## Preserve these boundaries

- Distinguish TA registration, rendering completion and display. Render-target reuse requires COMPLETE; render-to-texture never becomes DISPLAYED.
- Check mode/cable/stride/format bounds, scene ownership and shutdown wakeups before altering scanout or request state.
- Keep low-level PVR mechanisms separate from material/scene policy; inherited NAOMI 2 code is not fork hardware validation.
- Coordinate VRAM DMA uploads and reservations with storage/texture lifetimes rather than treating a pointer as sufficient ownership.

## Coordinate before changing

- Graphics2D/3D
- Assets/textures
- IRQ/cache/SQ and storage
- VBlank

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/video-mode-test` — candidate `make -C utils/video-mode-test test` in an assigned checkout.
- `utils/pvr-reservation-test` — candidate `make -C utils/pvr-reservation-test test` in an assigned checkout.
- `utils/pvr-multipass-layout-test` — candidate `make -C utils/pvr-multipass-layout-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/pvr/render_ticket`
- `examples/dreamcast/pvr/pipeline_status`
- `examples/dreamcast/video/mode-policy`
- `examples/dreamcast/pvr/multipass_dma`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Host packet acceptance and screenshots do not establish hardware render-cache visibility or display timing.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
