# PVR capability closure audit

This document records the graphics capabilities that should be implemented in
KOS itself. It is a behavioral inventory, not a compatibility-layer plan. New
interfaces must use KOS naming, ownership, error handling, synchronization, and
resource-allocation conventions.

## Scope and evidence policy

The audit compares the current PVR, video, DMA, store-queue, matrix, and image
facilities against the most complete late-generation development material and
hardware documentation available to the project. Older revisions and sample
programs are used only to resolve ambiguity or identify regressions.

Reference material is read-only. No reference headers, symbols, binary library
code, or proprietary prose belong in the implementation or its documentation.
Hardware behavior must be independently expressed and verified with host tests,
emulator tests, and eventually physical hardware.

## Existing KOS strengths

KOS already exposes the fundamental hardware model directly:

- five Tile Accelerator list types;
- direct store-queue submission and buffered DMA submission;
- polygon, sprite, modifier-volume, and two-volume polygon headers;
- packed-color, floating-color, textured, sprite, and modifier vertices;
- render-to-texture with an explicit render area and stride;
- texture-memory allocation, raw uploads, texture DMA, and store-queue uploads;
- twiddling for 4, 8, and 16 bits per pixel;
- VQ, mipmap, paletted, stride, rectangle, and YUV hardware format flags;
- palette, fog, punch-through, cheap-shadow, culling, blending, depth, and
  filtering controls;
- automatic and pre-sorted translucent modes;
- framebuffer access, checked video modes, blanking, dithering, and border
  color;
- basic pipeline timing and vertex-buffer utilization statistics.

These facilities are the foundation. Closure work should extend them rather
than place another renderer above them.

## Capability matrix

| Family | Current KOS state | Closure work |
| --- | --- | --- |
| Device and display lifecycle | Substantially covered | Replace assertion-only public failures with checked results where practical; expose missing scanout queries only when they describe stable hardware state. |
| Direct list submission | Covered | Preserve the low-overhead store-queue path. |
| Buffered list submission | Operational with checked writes | Add a checked companion for buffer assignment without breaking the existing pointer-returning API. |
| Hybrid submission | Covered | Preserve per-RAM-frame flushed-list ownership and prevent replay. |
| Pipeline status | Covered | Extend fields only for stable software or hardware state. |
| Completion and fault events | Covered | Preserve bounded IRQ-context dispatch, safe self-removal, and opt-in allocation. |
| Multi-pass scene control | No first-class pass object | Add a KOS pass description only after hybrid submission and status tracking are sound. Per-pass sort mode and user clipping are the essential behaviors. |
| User clipping | Covered | Preserve six-bit X/four-bit Y command bounds and active-target validation. |
| Global/pixel clipping | Covered per scene | Keep clip state attached to its scene across framebuffer and texture targets. |
| Background plane | Fixed color is public; plane representation is internal | Expose a checked background-plane description without leaking mutable driver state. |
| Texture allocation | General allocator covered | Add optional surface descriptors for dimensions, format, byte span, and ownership. Fixed-address and contiguous reservations should build on the existing allocator rather than introduce a second heap. |
| Texture upload | Full raw upload and DMA covered | Add checked partial rectangle uploads, mip-level uploads, codebook-only updates, and explicit completion objects. |
| Texture conversion | 4/8/16-bpp twiddling covered | Add 32-bpp handling where the hardware format permits it; keep offline VQ encoding outside the kernel unless a bounded, independently licensed encoder is justified. |
| YUV conversion | Low-level DMA covered | Add destination configuration, geometry validation, progress/completion reporting, and a safe asynchronous wrapper. |
| Palette and fog | Substantially covered | Add bulk checked palette updates and complete the fog-mode documentation and tests. |
| Render targets | Sized 16-bit target covered | Audit additional pixel modes, target switching, completion ordering, and texture hazards. |
| Display controls | Mostly available in `video` | Reuse the video subsystem; do not duplicate blanking, mode setting, border color, or dithering in PVR. Add only missing checked queries or synchronization. |
| Diagnostics and recovery | Logs and fixed 100 ms waits | Preserve fault details, distinguish timeout from hardware faults, provide reset/recovery only where register state can be restored safely. |
| Transform math | Existing KOS matrix API; external optimized library available | Keep transforms outside the PVR driver. Evaluate independent SH-4 math improvements separately and avoid making the renderer depend on an optional library. |

## Known concrete defects and incomplete paths

1. ~~`pvr_list_flush()` always asserts and returns failure despite being
   public.~~ Closed: it now transfers one buffered list synchronously and tracks
   the flushed state on the owning double-buffered RAM frame.
2. ~~Buffered submission relies on assertions for list bounds, pointer
   alignment, buffer capacity, and enabled-list checks.~~ Partially closed:
   primitive submission, list reuse, scene lifetime, and buffer capacity now
   report errors. The older buffer-assignment API still uses assertions and
   needs a checked companion rather than a source-incompatible signature
   change.
3. `pvr_txr_load_ex()` advertises flags that it rejects, including 32-bit input
   and on-the-fly VQ encoding; inverted preformatted uploads are also rejected.
4. ~~Render fault interrupts log text but do not preserve a queryable fault
   record or notify an application.~~ Closed: faults are latched with counters
   and TA register snapshots, and optional event handlers receive the fault.
5. The scene pipeline still has fixed waits. Sparse terminal-state reporting
   is closed by the coherent pipeline snapshot.
6. ~~User clipping is representable in a polygon header, but KOS provides no
   checked command-construction or submission helper.~~ Closed for direct and
   buffered lists, including active-target tile validation.
7. Texture subregion, mip-level, and codebook-only updates are absent.

## Dependency order

1. Complete and validate hybrid list submission.
2. Add typed pipeline status and fault capture.
3. Add user/global/pixel clipping helpers and background-plane description.
4. Add texture surface metadata plus checked full and partial transfers.
5. Add asynchronous texture/YUV completion objects.
6. Add pass descriptions over the completed list and clip primitives.
7. Expand render-target formats and display synchronization where hardware
   validation supports them.
8. Run a final API, examples, exports, documentation, and resource-cost audit.

## Validation gates

Every tranche needs:

- host tests for packing, layout, bounds, state transitions, and error mapping;
- a cross-build of KOS, addons, and every touched example;
- an emulator test that exercises both success and injected misuse paths;
- no eager thread, stack, or large-buffer allocation for optional features;
- explicit hardware-day items for behavior an emulator cannot establish.

Physical timing, overflow recovery, scanout transitions, and cache-visible DMA
remain hardware validation gates. Emulator success must not be described as
physical-hardware validation.

## Completed tranches

### Hybrid buffered/direct list submission

- flushed-list state is stored per double-buffered RAM frame;
- buffered lists can be transferred before scene completion without replay;
- direct store-queue lists remain usable in the same scene;
- invalid scene ordering, list reuse, alignment, and capacity failures are
  reportable;
- the focused example cross-builds and completes in interpreter-mode emulation,
  including its duplicate-flush misuse check.

The emulator result validates software state transitions and transfer ordering.
Physical DMA timing and contention remain hardware-day items.

### Pipeline status and persistent faults

- a coherent snapshot reports scene, DMA, registration, render, display,
  render-target, list, and buffer-index state;
- a monotonic sequence identifies software-visible transitions;
- ISP, strip, object-pointer-buffer, TA-input, and incomplete-DMA faults latch
  independently with occurrence counters;
- the latest fault preserves the raw interrupt event and TA buffer registers;
- selected fault flags can be cleared without racing a new interrupt;
- the focused example cross-builds and completes in interpreter-mode
  emulation, including null-output and invalid-mask misuse checks.

Physical fault injection, overflow behavior, and recovery remain hardware-day
items. Emulator validation covers the normal interrupt and snapshot paths only.

### Pipeline completion and fault events

- registration, render, display, DMA, and fault events share one opt-in handler
  vocabulary;
- callbacks run in documented IRQ context and allocate no worker or stack;
- registration order is stable and a callback may remove itself or a later
  handler without invalidating dispatch;
- the status example verifies callback-side snapshots, self-removal, invalid
  masks, stale handles, and normal completion counts;
- the hybrid-list example verifies one DMA event per explicit list transfer and
  rejects any DMA fault.

### Pixel and user clipping

- inclusive pixel rectangles are configured per scene before TA registration;
- clip state follows the correct framebuffer or texture render target through
  the pipeline instead of depending on mutable global state;
- packed 24-bit framebuffer clips reject odd coordinates explicitly;
- user-clip commands validate list type, coordinate ordering, six-bit X and
  four-bit Y hardware fields, and the active target's tile dimensions;
- commands submit through either buffered RAM lists or the currently open
  direct list without introducing a second list abstraction;
- the focused example validates command words, ordering failures, target
  bounds, disabled lists, persistent zero-fault state, and 120 rendered frames
  in interpreter-mode emulation.
