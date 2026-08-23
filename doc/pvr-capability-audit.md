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
| System configuration and lifecycle | Core initialization is covered | Add checked companions instead of changing established assertion-based entry points; do not create a second device owner. |
| Display modes and scanout | Mode setting, blanking, border, dithering, and framebuffer access are covered by `video` | Add stable scanline, horizontal-blank, filter, and surface queries through `video`; do not duplicate them in PVR. |
| Timing and callbacks | VBlank and PVR completion/fault events are covered | Audit horizontal-blank and scanline scheduling. Preserve bounded IRQ callbacks and opt-in allocation. |
| Direct list submission | Covered | Preserve the low-overhead store-queue path. |
| Buffered list submission | Checked writes and hybrid flushing are covered | Finish checked buffer assignment and preserve per-RAM-frame ownership. |
| Multi-pass scene control | No first-class pass sequence | Model each pass as a complete TA registration bank with per-pass list memory, sort policy, direct-list selection, and modifier-list mapping. A begin/end wrapper alone is insufficient. |
| User and pixel clipping | Covered per scene | Keep clip state attached to its scene across framebuffer and texture targets. |
| Background plane | Covered for untextured color planes | Textured backgrounds remain optional because ordinary geometry expresses them without special scene state. |
| Global material state | Fog and cheap-shadow controls are broad; clamp endpoints and punch-through threshold were missing | Provide checked packed-color clamp, threshold, bulk palette, and header supersampling controls. Audit culling threshold and vertical-filter coefficients separately. |
| Context and header construction | Most polygon, sprite, modifier, two-volume, blend, depth, fog, UV, palette, and texture fields are covered | Preserve public context layout. Add extended compilation flags for missing packed-header controls; treat individual header mutation helpers as optional optimizations. |
| Render submission identity | Stable allocation-free tickets cover queued registration, completed registration, rendering, target completion, and display | Preserve identity-specific waits and expose counters in coherent pipeline diagnostics. |
| Render targets | Sized 16-bit render-to-texture, checked surface binding, and explicit completion/no-flip contracts are covered | Audit additional output formats and checked framebuffer bindings. |
| Texture allocation | General allocator and checked caller-owned surfaces are covered | Add contiguous multi-surface reservations for demonstrated streaming and batch-conversion needs. Do not move raw pointer allocations behind an unsafe collector. |
| Texture formats | Linear, twiddled, mipmapped, paletted, full-codebook VQ, stride, rectangle, and YUV are represented | Validate reduced-codebook VQ and any VQ/palette combinations against hardware before extending the surface layout vocabulary. |
| Texture upload and readback | Checked synchronous and immediate-admission asynchronous uploads plus encoded CPU readback are covered | Preserve explicit render/sampling hazard rules and add asynchronous readback only for a demonstrated need. |
| Texture conversion | Checked 4/8/16-bpp twiddling is covered | Keep palette creation and VQ encoding in content tools rather than placing unbounded encoders in the kernel. |
| YUV conversion | Checked single-destination synchronous and asynchronous conversion is covered | Add contiguous multi-destination conversion only with bounded geometry, progress, and ownership. |
| Palette and fog | Palette format, single entries, fog colors, density, and table generation are covered | Complete checked bulk palette writes plus fog-mode documentation and packing tests. |
| Diagnostics and recovery | Coherent status, fault latching, and events are covered | Replace remaining fixed waits only where a safe recovery sequence is known. |
| Transform math | Existing KOS matrix API; optional optimized math can remain external | Keep transforms outside the PVR driver. A future scene library may consume both without making the driver depend on either. |

## Known concrete defects and incomplete paths

1. ~~`pvr_list_flush()` always asserts and returns failure despite being
   public.~~ Closed: it now transfers one buffered list synchronously and tracks
   the flushed state on the owning double-buffered RAM frame.
2. ~~Buffered submission relies on assertions for list bounds, pointer
   alignment, buffer capacity, and enabled-list checks.~~ Closed without a
   source break: the established buffer-assignment function retains its
   contract and a checked companion rejects invalid, disabled, or still-owned
   buffers.
3. ~~`pvr_txr_load_ex()` unconditionally twiddles despite preformatted flags and
   exposes no checked failure path.~~ Closed: the compatibility wrapper now uses
   an error-reporting implementation that distinguishes conversion from
   preformatted copies and reports unsupported runtime VQ encoding or
   nonblocking operation explicitly.
4. ~~Render fault interrupts log text but do not preserve a queryable fault
   record or notify an application.~~ Closed: faults are latched with counters
   and TA register snapshots, and optional event handlers receive the fault.
5. The scene pipeline still has fixed waits. Sparse terminal-state reporting
   and per-render identity are closed by coherent pipeline snapshots and
   allocation-free completion tickets; bounded recovery for established waits
   remains separate work.
6. ~~User clipping is representable in a polygon header, but KOS provides no
   checked command-construction or submission helper.~~ Closed for direct and
   buffered lists, including active-target tile validation.
7. ~~Texture subregion, mip-level, codebook-only, asynchronous, and checked YUV
   updates are absent.~~ Closed with bounded synchronous operations plus
   immediate-admission asynchronous request objects.
8. ~~Packed global clamp endpoints, punch-through threshold, checked bulk
   palette writes, and high-level texture supersampling were inaccessible or
   incomplete.~~ Closed with checked register APIs, bounded palette ranges, and
   extended compilers that preserve established context layouts.

## Dependency order

1. ~~Complete and validate hybrid list submission.~~
2. ~~Add typed pipeline status, persistent faults, and events.~~
3. ~~Add clipping and background-plane descriptions.~~
4. ~~Add texture surfaces plus checked synchronous and asynchronous updates.~~
5. ~~Close global material and packed-header state needed by higher-level scene
   generation.~~
6. ~~Implement opt-in direct multi-pass registration banks while preserving the
   existing one-pass fast path.~~ Closed for direct store-queue submission with
   one through eight independently binned passes, exact preflight, hardware
   continuation, final-only render release, a host layout suite, and an
   emulator-tested three-pass example. Per-pass DMA staging, IRQ-chained
   continuation, and pass-aware hybrid early flushing are also closed.
7. ~~Add per-render completion identity and finish render-target/no-flip hazard
   contracts.~~ Closed with monotonic allocation-free tickets, five observable
   stages, identity-specific waits, exact texture-target geometry, and explicit
   separation between render completion and framebuffer display.
8. Add only hardware-validated advanced texture layouts, contiguous
   reservations, and batch YUV conversion. Checked encoded CPU readback and
   compatible 16-bit surface render binding are closed; physical render-target
   visibility remains a hardware validation gate.
9. Close remaining display timing and filter controls in `video`.
10. Run the final API, examples, exports, documentation, and resource-cost
    audit before defining an optional higher-level scene interface.

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

### Identity-specific render completion

- a successful tracked scene finish returns a caller-owned ticket with a
  monotonic nonzero render identity and no allocation or worker;
- queued, registered, rendering, complete, and displayed counters advance at
  their corresponding TA, ISP/TSP, and VBlank transitions;
- waits use the requested identity, so an unrelated interrupt or earlier frame
  cannot satisfy a later ticket;
- render-to-texture tickets preserve the exact target pointer, width, height,
  and stride, define `COMPLETE` as the reuse boundary, and reject the
  framebuffer-only display stage;
- framebuffer tickets distinguish rendering completion from the later VBlank
  page flip;
- shutdown wakes every stage wait and invalid active pipeline slots never move
  historical completion counters backward;
- the focused example completes 60 texture/framebuffer pairs in interpreter
  and dynamic-recompiler emulation, including invalid-output, unsupported-stage,
  forged-future-ID, target-metadata, and pipeline-reconciliation checks.

Physical completion visibility and page-flip timing remain hardware-day items.

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

### Background-plane geometry

- an active scene can replace the default solid plane with three checked
  screen-space vertices, independent RGB888 colors, and explicit positive
  depth;
- background state is copied into the scene pipeline before TA registration,
  preventing a later scene from mutating an in-flight render;
- the established solid-color and depth setters remain source compatible and
  update an active scene only while it is still configurable;
- the focused example validates copy-out, finite/depth/color bounds, late-update
  rejection, zero persistent faults, and 120 frames in interpreter-mode
  emulation.

### Texture surface metadata and bounded transfers

- caller-owned surface descriptors record VRAM capacity, format, storage
  layout, top-level dimensions, complete mip count, codebook size, and exact
  encoded size without allocating a manager object;
- surfaces can own an allocation from the established PVR memory allocator or
  bind caller-managed VRAM with explicit capacity checking;
- every transfer revalidates the caller-owned descriptor's address and declared
  capacity against the complete 64-bit CPU-visible VRAM window;
- logical mip level zero names the largest image while byte offsets describe
  the hardware's smallest-first storage order;
- full VQ surfaces reserve a 2048-byte codebook and expose separate checked
  codebook and index-level uploads;
- CPU, store-queue, and blocking DMA byte-range uploads enforce their actual
  alignment and capacity contracts;
- rectangular source data updates linear storage directly and converts 4-, 8-,
  or 16-bit uncompressed texels into twiddled storage without a temporary
  full-texture buffer;
- encoded byte-range and level uploads remain available for layouts, such as VQ
  and twiddled YUV, where an unencoded rectangle is not self-describing;
- encoded full, byte-range, and mip-level readback uses the same validated
  layout metadata and copies from the uncached CPU-visible VRAM alias;
- a nonmipmapped linear or X32-stride surface whose format matches the active
  16-bit render mode can begin a checked texture-target scene, with dimensions,
  stride, and target identity preserved by its render ticket;
- the host regression test passes exact plain, strided, mipmapped, palette, and
  VQ size and offset vectors, plus invalid layout combinations and descriptor
  corruption;
- the focused example completes 120 frames in interpreter-mode emulation after
  exercising checked twiddling, an asynchronous DMA upload, exact upload
  readback, a checked render target and ticket, live rectangle updates,
  overflow and type misuse, and zero persistent PVR faults.

Physical DMA timing, CPU visibility after hardware render completion, and
updates concurrent with texture sampling remain hardware validation items.

### Asynchronous texture and YUV requests

- full, byte-range, mip-level, and codebook texture DMA operations return an
  opaque request with coherent byte progress, terminal errno, timed wait, and
  explicit destruction;
- admission is nonblocking: an occupied shared PVR DMA channel reports `EBUSY`
  instead of allocating queued work or blocking the caller;
- accepted operations advance entirely from interrupt context, with no pump,
  carrier thread, permanent buffer, or allocation beyond the request itself;
- YUV420 and YUV422 input geometry is checked against a nonmipmapped linear
  YUV422 destination, including exact macroblock ordering and byte counts;
- YUV terminal state requires both channel-2 source completion and the separate
  converter-done event, and remains correct if the two interrupts are observed
  in either order;
- PVR shutdown removes completion handlers before cancelling an admitted
  request, releases shared DMA ownership, and wakes request waiters;
- host vectors cover both macroblock formats, maximum dimensions, incompatible
  surface storage, illegal geometry, and invalid arguments;
- the focused example cross-builds and completes 120 frames in interpreter-mode
  emulation after ordinary asynchronous DMA and a one-block YUV conversion,
  with no reported SH-4 exception or persistent PVR fault.

Emulation verifies software state transitions and the emulator's interrupt
model. Physical interrupt ordering, DMA/converter timing, cache visibility, and
concurrent texture sampling remain hardware validation items.

### Global material and header state

- packed ARGB8888 clamp endpoints have checked set and readback operations,
  including per-channel endpoint validation;
- the eight-bit punch-through alpha threshold has checked set and readback
  operations without changing the previous initialization value;
- one bounded palette operation updates any contiguous subset of the 1024-entry
  hardware table and rejects overruns before the first write;
- extended polygon, sprite, and two-volume compilers expose the packed texture
  supersampling bit without changing context structure sizes or adding work to
  the established compiler path;
- checked vertex-buffer assignment validates list state, alignment, total
  allocation size, active scenes, and RAM frames still queued against the old
  allocation;
- every new operation is present in the Dreamcast export table, and the full
  KOS rebuild plus the focused example cross-build complete successfully.

The focused ELF boots and runs in emulation without an observed SH-4 exception.
That host run did not expose the program's serial completion line, so it is not
recorded as a full runtime pass. Clamp timing during a live render,
supersampling quality, and palette visibility remain physical-hardware gates.
