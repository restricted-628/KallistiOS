# PVR Multipass Registration Design

## Purpose

This document defines a native KOS design for registering several geometry
passes with the tile accelerator and rendering them as one scene. It is an
implementation contract, not a compatibility API.

The existing one-pass API remains the default. Applications that do not opt
into multipass registration retain the current memory layout, state machine,
and submission cost.

## Hardware model

Multipass registration is one tile-accelerator session followed by one
renderer submission:

1. Initialize the tile accelerator once.
2. Register the enabled lists for pass zero.
3. Wait until every enabled list in that pass has reached its end marker.
4. Select the next pass's object-pointer-block base and allocation policy.
5. Continue list registration without resetting the shared parameter cursor or
   overflow cursor.
6. Repeat until the final pass is complete.
7. Start the renderer once, using one region array that describes every pass.

Continuation deliberately preserves the current parameter write position and
the next overflow-block position. Reinitializing the tile accelerator between
passes would break the shared display list and is not multipass rendering.

The region array is tile-major. All pass entries for tile zero are consecutive,
then all pass entries for tile one, and so on. Each entry contains one control
word followed by the five list pointers:

```
tile 0: pass 0, pass 1, ... pass N-1
tile 1: pass 0, pass 1, ... pass N-1
...
```

The control word uses these fields:

- bit 31: final region; set only on the final pass of the final tile;
- bit 30: preserve depth; clear on pass zero, set on later passes;
- bit 29: translucent list is pre-sorted for this pass;
- bit 28: preserve the tile accumulation result; set on every pass except the
  final pass;
- bits 13 through 8: tile Y;
- bits 7 through 2: tile X.

An absent list pointer is `0x80000000`. A present pointer names that pass's
initial object-pointer block for the tile and list.

## Public API

The implementation adds three opt-in operations without changing the layout or
behavior of `pvr_init_params_t`:

```
int pvr_init_multipass(const pvr_init_params_t *common,
                       const pvr_pass_config_t *passes,
                       size_t pass_count);

int pvr_scene_next_pass(void);

int pvr_set_pass_vertbuf_checked(size_t pass, pvr_list_t list,
                                 void *buffer, size_t len,
                                 void **old_buffer);

```

`pvr_pass_config_t` contains the five OPB sizes and the translucent sort policy
for one pass. The supported pass count is one through eight. A count of one
uses the same region and registration behavior as direct `pvr_init()`.

`pvr_scene_next_pass()` closes the current pass, establishes the required
registration boundary, and admits the next pass. It fails on the configured
final pass. `pvr_scene_finish()` closes the final pass and queues the one
renderer submission.

Directly submitted lists need no main-RAM vertex buffer. Buffered submission
requires a pass-specific allocation for each enabled list. The established
checked vertex-buffer operation continues to address pass zero.

The API does not expose pass objects, copied context structures, or foreign
work-area conventions. Higher-level scene systems can build over this state
machine without becoming part of the low-level driver.

## Resource model

Multipass storage is allocated only by `pvr_init_multipass()`.

For each of the two TA frame banks, VRAM contains:

```
shared parameter buffer
combined initial OPB areas for every pass
shared overflow OPB area
region-array header
tile-major region entries for every pass
frame buffer
```

The initial OPB size is the sum of the enabled per-list bin sizes across every
pass and every tile. The continuation base for pass `p` is the initial OPB base
plus the sizes of passes `0` through `p - 1`. The shared overflow area begins
after all initial pass areas. The existing overflow-count policy applies to the
combined initial area.

The parameter buffer remains shared because continuation preserves the TA
parameter cursor. It therefore keeps the established `vertex_buf_size`
meaning: the total VRAM parameter capacity for the complete scene.

DMA staging metadata is per frame, pass, and list and exists only in the small
opt-in multipass control allocation. The application owns the actual staging
memory. No worker thread or permanent large buffer is added.

Initialization must reject a layout that crosses either 4 MiB frame-bank
boundary. Failure is reported before clearing VRAM or changing live PVR state.

## State machine

The scene tracks both a build pass and a hardware registration pass.

```
IDLE
  -> BUILDING(pass 0)
  -> BUILDING(pass 1) ... BUILDING(pass N-1)
  -> QUEUED
  -> REGISTERING(pass 0)
  -> CONTINUING(pass 1) ... CONTINUING(pass N-1)
  -> RENDERING
  -> COMPLETE
```

Direct submission combines `BUILDING` and `REGISTERING`, as the one-pass path
already does. DMA submission finishes building all passes before registration
begins and reuses the same hardware pass-boundary invariants from IRQ context.

At an intermediate boundary:

- every enabled list for the current pass must be closed;
- every list end interrupt for that pass must be observed;
- no TA-input DMA may remain active;
- rendering must remain inhibited;
- the next pass's OPB base and allocation mask are programmed;
- the continuation register is written and read back;
- per-pass closed, transferred, flushed, and DMAed masks are reset;
- only then may the next pass feed the TA.

At the final boundary, the existing render queue is released instead of
continuing registration.

## IRQ and synchronization invariants

The list-completion IRQ path now compares against the active pass's enabled
mask and applies a final-pass test before releasing the renderer.

An intermediate list-complete interrupt wakes the direct-submission thread or,
for buffered submission, continues the TA and starts the next pass's DMA chain.
It does not toggle `ta_target`, synchronize a new registration bank, clear
`ta_busy`, or start the renderer.

The continuation register sequence and the related OPB base/allocation writes
must be IRQ-serialized with list-completion handling. Application code must
never write continuation while the TA is still accepting the previous pass.

Registration-complete events remain scene-level for compatibility. A new
pass-complete event may be added only if it carries the pass index and cannot
be mistaken for final scene registration.

## Submission modes

### Direct

`pvr_scene_next_pass()` closes missing lists, waits for the current pass's list
completion, performs continuation, and returns with the next pass ready for
store-queue submission.

### DMA

Each pass has independent list staging pointers and lengths. Scene construction
advances through passes in main RAM. When the frame is queued, the DMA chain
feeds pass zero. At the end of a hardware pass, continuation occurs before the
first DMA for the next pass begins.

### Hybrid

Not yet exposed for multipass initialization.

Direct and buffered lists may coexist in a pass. A pass boundary waits for both
the already-flushed direct/buffered lists and any remaining DMA lists. A list
that was flushed early is never replayed at scene completion.

## Compatibility and failure rules

- `pvr_init()` and every existing one-pass scene function retain their ABI and
  fast path.
- Multipass initialization is rejected for null configurations, zero passes,
  more than eight passes, invalid bin sizes, an all-disabled pass, arithmetic
  overflow, or a VRAM layout that does not fit.
- `pvr_scene_next_pass()` reports `EPERM` outside an active scene, `EALREADY`
  on the final pass, and propagates timeout or hardware-fault results from the
  registration boundary.
- A fault during an intermediate pass makes continuation fail with `EIO`.
  Rendering a partial pass chain is not permitted.
- Shutdown cancels an unfinished multipass scene before freeing its small
  control allocation.

## Validation plan

1. ~~Add a host test for combined OPB offsets, tile-major region ordering,
   absent-list markers, and all control-word transitions for one through eight
   passes.~~
2. ~~Verify that the one-pass generated region array is byte-for-byte identical
   to the existing layout.~~
3. ~~Add a direct-submission example that draws distinguishable geometry in at
   least three passes and verifies completion/fault state.~~
4. ~~Add a DMA example with separate per-pass buffers.~~ Add a hybrid variant
   only after early-flush pass ownership is implemented.
5. Inject invalid pass transitions, undersized buffers, disabled lists, and a
   deliberately insufficient VRAM configuration.
6. Cross-build the full tree and verify exports and generated documentation.
7. Exercise all examples in an emulator for state-machine failures and
   exceptions.
8. Retain physical-hardware validation gates for timing, overflow behavior,
   sort interaction, and accumulation/depth preservation.

## Deferred features

Per-pass tile clip rectangles, modifier-list remapping, and alternate list
routing require separate checked contracts. They must not be inferred from a
compatibility surface or added as untyped flag words. The core implementation
above provides the registration-bank foundation they need.
