# PVR render-ticket contract

PVR render tickets identify one submitted scene without allocating a request
object, worker, queue, or callback. They make the difference between renderer
completion and framebuffer display observable while leaving the established
`pvr_scene_finish()` path unchanged.

## Lifecycle

Call `pvr_scene_finish_tracked()` in place of `pvr_scene_finish()` when a scene
needs an identity. A successful call fills a caller-owned
`pvr_render_ticket_t`; there is no destroy operation. IDs are nonzero and
increase monotonically until PVR shutdown.

The ticket records whether the scene targets a framebuffer or texture. Texture
tickets also preserve the exact destination pointer, rendered width and height,
and memory stride that were accepted for that scene.

| Stage | Guarantee |
| --- | --- |
| `PVR_RENDER_STAGE_QUEUED` | The completed scene has entered the TA pipeline. |
| `PVR_RENDER_STAGE_REGISTERED` | The TA has completed the scene's final registration bank. |
| `PVR_RENDER_STAGE_RENDERING` | The ISP/TSP has started rendering this scene. |
| `PVR_RENDER_STAGE_COMPLETE` | The ISP/TSP no longer writes this scene's target. |
| `PVR_RENDER_STAGE_DISPLAYED` | A framebuffer result has become the scanout source at VBlank. |

`pvr_render_ticket_get_stage()` is a nonblocking snapshot.
`pvr_render_ticket_wait()` waits for a specific ID rather than a global frame
counter. A zero timeout has no deadline; a nonzero timeout is a total
millisecond budget across all wakeups.

## Target hazards

`COMPLETE` is the ownership boundary for a render target. Before that stage,
the application must not read, upload over, release, or reuse the destination.
This applies even if a later CPU or DMA operation would touch only part of the
same allocation, because the hardware render extent is the ticket's complete
target geometry.

For render-to-texture, `COMPLETE` is terminal. The texture never enters the
framebuffer page-flip path, so requesting `DISPLAYED` returns `ENOTSUP`.
Sampling the completed texture in a later scene is permitted after the wait;
sampling it from geometry whose rendering overlaps the producing render is not.

For framebuffer output, `COMPLETE` means drawing has stopped but does not mean
the frame is visible. Wait for `DISPLAYED` when application logic depends on
the page flip itself, such as scanout capture or presentation accounting.

## Diagnostics and shutdown

`pvr_get_pipeline_status()` reports the current and most recently completed
IDs for each stage. Active scene, TA-registration, ISP/TSP-render, and pending
display slots use zero when idle. Historical stage counters never move
backward, including when an unexpected completion interrupt has no active ID.

Tickets cease to be usable after `pvr_shutdown()`. Blocked waiters are woken
and return `ENODEV`; an ID that was never queued returns `ENOENT`. The scene API
remains application-serialized as before, but ticket queries and waits protect
their pipeline snapshots against interrupt updates.

## Validation boundary

The focused example alternates texture and framebuffer renders, validates exact
target metadata, waits for the two different terminal stages, checks misuse,
and reconciles the final ticket IDs with the coherent pipeline snapshot. It
passes in both interpreter and dynamic-recompiler emulation. Physical scanout
timing, render-cache visibility, and texture sampling immediately after a real
hardware completion interrupt remain hardware validation items.
