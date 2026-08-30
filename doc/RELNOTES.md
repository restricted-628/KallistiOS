KallistiOS ##version##  
Copyright (C) 2002, 2003 Megan Potter  
Copyright (C) 2012-2019 Lawrence Sebald  
Copyright (C) 2024-2026 Donald Haase  
Copyright (C) 2025 Eric Fradella  
Copyright (C) 2026 Joseph Black

UNRELEASED DREAMCAST CAPABILITY WORK
------------------------------------

This development series extends existing KOS drivers and retains their normal
lifecycle and naming conventions.

* Added allocation-free scalar-band geometry for cel shading. One generic
  partitioner covers binary and arbitrary ordered multiband shading, preserves
  position, normalized normal, decoded floating UVs, base/offset color, and
  exact threshold shade, and deterministically intersects oppositely traversed
  shared edges. Small checked helpers supply signed directional shade profiles,
  band lookup, capacity bounds, and packed-color modulation without adding
  model records, renderer state, or per-frame allocation.
* Added an admitted vertex-quantized toon ramp beside exact geometric band
  splitting. It performs logarithmic shade lookup without per-vertex ramp
  validation and independently modulates packed base and offset colors, giving
  custom and compact renderers a smaller approximate diffuse/highlight policy
  while retaining exact topology-changing emission when required.
* Classified deferred polygon capture/draw controls as an explicit compact-
  model canonicalization requirement. Validation and inspection report both
  record counts, while runtime emitters continue rejecting the cross-hierarchy
  execution mechanism before side effects instead of creating a competing
  on-console polygon cache.
* Added finite floating-point UV strip records as a rare compact-model escape
  beside signed 8.8 and 6.10 coordinates. The model converter retains 6.10 as
  its first choice and 8.8 for wider tiling, selecting float UVs only when the
  authored range exceeds both fixed encodings instead of rejecting or
  clamping it.
* Added a prepared two-volume compact-cache band path. It preserves both
  texture-coordinate, diffuse, and offset-color parameter sets through shade
  subdivision and homogeneous frustum clipping, supports independent
  outside/inside modulation ramps, and emits the original format-bound PVR
  packets without selecting modifier state on the CPU.
* Added SH4ZAM-backed normal extrusion and a prepared compact-cache inverted-
  shell outline pass. Smooth and flat-shaded strips preserve current
  deformation, optional per-frame policy, frustum clipping, and established
  geometry sinks while the application retains explicit culling, material,
  scene, and list ownership.
* Added checked homogeneous line-segment clipping and constant-width
  screen-space line expansion, plus an allocation-free prepared compact-model
  wireframe policy. It enumerates unique full-mesh, outside-boundary, or
  consecutive-path strip edges; composes current deformation, clipping, and
  existing geometry sinks; and retains caller ownership of scratch, materials,
  scenes, and lists.
* Added a caller-owned compact-model render-policy binding with unlit,
  diffuse, and diffuse-plus-specular presets. It composes homogeneous position
  handling, vertex intensity, signed lights, material ambient and exponent,
  optional environment UV generation, optional distance-cue alpha, material
  submission, and an application callback without adding model records,
  global renderer state, allocation, or scene ownership. Lighting context is
  admitted once rather than revalidated for every bound vertex.
* Added an allocation-free extended PVR vertex-lighting kernel beside the
  established minimal Lambert path. It accumulates signed diffuse lights
  before saturation, generates checked Blinn-Phong offset color from positive
  lights, multiplies per-vertex diffuse/specular material values, and can
  encode a caller-defined distance cue into vertex alpha. The original API and
  its nonnegative-light contract remain unchanged.
* Added a checked view-normal-to-UV environment-map kernel and a composable
  compact-model callback adapter. Environment strips now have a standard
  executable policy which preserves per-reference normal discontinuities,
  transforms normals correctly under nonuniform object scale, forwards
  material submission, and permits a chained application vertex policy to
  override generated coordinates or combine lighting.
* Added time-aware Catmull-Rom interpolation for scalar and four-component
  animation tracks. Uneven key spacing contributes to the spline tangents,
  endpoint behavior is explicit, and quaternion and Boolean tracks retain
  their established spherical and step-only contracts.
* Added arbitrary-count compact skinning beside the existing four-joint fast
  path. A canonical span/weight representation preserves every influence,
  expands once into caller-owned runtime storage, and uses a checked general
  deformation kernel with the same palette, overlap, prefix, and pose-lookup
  contracts. The previously public Compact Skin4 entry points are now also
  available to loadable modules.
* Added topology-preserving near-plane warping for PVR modifier triangles and
  compact modifier-volume models. Crossing points move to the caller's near-W
  plane without adding or dropping triangles, so closed shadow volumes cannot
  acquire clipping holes; prepared and immediate model paths share the same
  checked implementation.
* Added explicit compact-model frustum policies for ordinary strips. Fully
  outside models return before stream traversal, fully inside models retain
  the fast strip path, and intersecting models can either split triangles with
  UV/color interpolation or drop crossing triangles. A trusted-visible mode
  preserves the minimum-overhead path for caller-proven content.
* Added exact object-space sphere classification against homogeneous PVR
  frusta and a compact-model wrapper for retained model bounds. Applications
  can reject whole models before stream traversal and identify fully visible
  models before selecting more expensive per-triangle clipping paths.
* Added a validated compact-volume triangle iterator shared by model tools,
  collision consumers, modifier rendering, and prepared modifier caches. It
  expands triangle, quad, and strip records with consistent winding while
  preserving all per-triangle user words and exact record boundaries.
* Added signed fixed-point compact-model texture coordinates. New 8.8 records
  provide wide repeat range and new 6.10 records provide finer precision while
  retaining the same 16-bit coordinate storage. The model converter selects
  6.10 when representable and falls back to 8.8 for wide tiling; negative and
  repeated coordinates are accepted. Existing unsigned-normalized records
  remain readable but are no longer emitted by the converter.
* Added an independently managed expansion-bridge lifecycle and a fixed-state,
  generation-checked allocator for its complete 32 KiB SRAM window. The BBA
  now leases and releases its established receive, wrap-guard, and transmit
  ranges instead of owning hard-coded global addresses. G1 and G2 DMA claims
  exclude one another on each lease.
* Extended G2 DMA root-bus endpoints to both PVR-RAM apertures with explicit
  region status and cache behavior. Direct optical DMA likewise accepts
  system or PVR RAM, and adds synchronous/asynchronous reads into leased
  bridge SRAM for a serialized disc-to-SRAM-to-RAM or VRAM pipeline.
* Added true 64-channel synchronized AICA key-on and routed stereo effects and
  streams through it. The ARM firmware now validates command sizes, channel
  parameters, loop geometry, and sound-RAM ranges before programming hardware;
  malformed queue records cannot trap its command loop. PCM stream maxima now
  fit the 16-bit hardware loop endpoint after alignment. These changes add no
  service thread, allocation, or periodic work.
* The bundled AICA firmware now reports a versioned capability and health
  snapshot. Sound initialization rejects silent or incompatible firmware, and
  applications can inspect queue pressure and rejected or malformed commands
  through a bounded public status query.
* Added lazily initialized asynchronous sound-RAM transfer requests with
  queued bidirectional DMA, exact-byte PIO fallback, live byte progress,
  execution deadlines, cancellation, waits, and thread-context completion
  callbacks. Synchronous SPU transfers retain their established behavior and
  applications that do not submit a request allocate no worker or queue state.
* Bundled SH4ZAM 0.8 as a first-class, automatically built Dreamcast math
  component and added it to the standard grouped KOS link set. The optimized
  target backend remains independently attributed under its MIT license and
  retains normal section garbage collection, so unused routines occupy no
  application image space. Added compile-time layout checks and alias-safe
  bridges between established KOS matrices/vectors and SH4ZAM types.
* Routed memory matrix composition through SH4ZAM's one-off FIPR transform
  path without changing XMTRX. Batched checked PVR projection now keeps its
  transform resident in XMTRX across the stream and restores the caller's
  prior matrix on both success and partial failure.
* Added opt-in fiber math contexts. A thread attaching with
  `KFIBER_ATTACH_MATH_CONTEXT` receives an independent XMTRX image for its main
  fiber and each child fiber. Lightweight attachment remains the default and
  performs no matrix allocation or matrix save/load during a switch.
* Added checked, caller-owned PVR material packets compiled from existing
  polygon, sprite, and two-volume contexts. Complete validation precedes
  publication, and submission continues through established PVR list paths.
* Added caller-owned screen/W frusta with bounded AABB classification and
  allocation-free homogeneous triangle clipping. Clipped polygons expand into
  independent canonical PVR triangles with explicit output capacity.
* Added checked global small-polygon culling-threshold control and a defined
  1.0f initialization value for every PVR session.
* Added optional caller-owned compact-model plans with sparse page-backed,
  constant-time vertex resolution. Prepared ordinary, two-volume, and modifier
  emitters retain the immediate path's validation, callbacks, progress, and
  sinks while applications that do not prepare a model allocate no index.
* Added caller-owned compact-model draw caches for ordinary strips, two-volume
  strips, and modifier volumes. One-time admission retains decoded strip state
  or expanded volume topology, tightly packed 32-byte or 64-byte PVR packets,
  canonical deformation inputs, original vertex indices, and modifier user
  words in exact queried footprints. Repeated emission reads neither compact
  stream and can bind caller-owned skin or morph results plus per-frame policy
  without creating a scene owner, worker, allocator, or hidden resource
  namespace. Ordinary and two-volume strips retain exact reference-pose AABBs;
  filtered emitters can skip a strip before deformation, material setup,
  projection, callbacks, or sink publication, while dynamically deformed
  models retain explicit responsibility for conservative current-pose policy.
* Added explicit compact-model skin bindings with four normalized influences
  per vertex, complete model and joint coverage validation, caller-owned
  constant-time pose lookup, and one-time canonical source construction for
  repeated use by the existing checked deformation kernel.
* Added explicit compact-model shape bindings with sparse canonical morph
  deltas, complete target-to-base validation, caller-owned constant-time pose
  lookup, one-time dense source construction, and direct scalar animation-
  channel binding into the existing checked morph kernel.
* Added versioned compact-model asset containers with independently stored
  vertex and polygon sections, bounded workspace queries, header and decoded-
  stream CRCs, raw zero-copy loading, and optional codec callbacks. The model
  converter can emit raw assets or LZ4 Frame-compressed vertex partitions.
* Bundled the unmodified BSD-2-Clause LZ4 1.10.0 library as an optional static
  addon, including its Frame and dictionary APIs and a strict compact-asset
  decoder callback. Applications that do not link it incur no runtime cost.
  The addon also provides manually budgeted incremental decoding and a separate
  opt-in adapter for caller-configured shared fiber-service executors; neither
  policy reserves a thread, stack, queue, or workspace until selected.
* PVR TA startup now propagates bounded readiness failures instead of
  continuing into a busy or faulted accelerator, and AICA store-queue paths
  acquire their real mapped destination under the checked SQ contract.
* Added shared, fail-closed G1 controller arbitration for GD-ROM and ATA.
  Drivers now use the public G1 ownership API instead of the former private
  `_g1_ata_sem` symbol.
* Completed buffered PVR list flushing and hybrid per-list submission, allowing
  selected lists to use RAM/DMA while other lists use direct store-queue input
  in the same scene without replaying early transfers. Scene completion now
  preflights space for every required end marker before modifying any caller
  buffer, so a full buffer fails with `ENOSPC` instead of overrunning storage.
* Added coherent PVR pipeline snapshots and persistent fault records with
  per-fault counters and captured TA buffer registers. Fault interrupts are
  monitored in all builds while their debug logging remains optional.
* Added opt-in PVR registration, render, display, DMA, and fault events with
  bounded IRQ-context callbacks, safe self-removal, and no worker thread or
  allocation until a handler is registered.
* Added checked per-scene pixel clipping for framebuffer and texture targets,
  plus bounded tile-granular user-clip command compilation and submission for
  both direct and buffered polygon lists.
* Added checked per-scene background-plane geometry, RGB888 vertex colors, and
  depth state while preserving the established solid-color interface.
* Added coherent physical scanout snapshots, checked framebuffer display
  filters with exact vertical coefficients, and opt-in physical-line raster
  callbacks with exclusive event ownership. The established dithering setter
  now preserves unrelated framebuffer-control fields. Full-scene
  antialiasing remains owned by PVR initialization because it changes the TA
  render-buffer layout as well as a display-scaler bit.
* Checked video-mode validation now rejects horizontal or vertical timing
  counters that do not fit their ten-bit hardware fields, including the
  effective scanline value after VGA line doubling.
* Added checked framebuffer-surface queries for configured slots, the hardware
  scanout target, and KOS's CPU drawing target, including exact geometry, VRAM
  offsets, visible byte counts, known capacity, and interlaced field layout.
* Added caller-owned PVR texture surfaces with checked allocation or external
  binding, exact mip and VQ codebook layout metadata, format-word generation,
  bounded full/partial/level/codebook transfers, and rectangular updates for
  linear and twiddled uncompressed textures. Encoded full, byte-range, and
  mip-level CPU readback is bounded by the same metadata, while a checked
  render-target entry point accepts only compatible 16-bit linear surfaces and
  records exact target geometry through render tickets.
* Added a checked full-codebook VQ palette builder, allowing byte-indexed
  textures to carry independent 16-bit color tables without consuming global
  palette banks. A focused example demonstrates the doubled-dimension VQ
  layout and validates 120 rendered frames.
* Added compact-codebook VQ surfaces with exact encoded-size metadata, checked
  high-range index bases, and distinct storage and texture-header addresses.
  Allocated, externally bound, and reserved surfaces retain ordinary upload,
  readback, release, and compact-model material behavior while storing only
  the codebook entries actually used.
* Added opt-in fixed-slot texture residency over one contiguous VRAM
  reservation. Caller-owned slot and surface arrays provide deterministic LRU
  replacement, explicit render-safe pins, generation-checked stale-handle
  rejection, two-phase asynchronous upload publication, and coherent cache
  statistics without a worker, queue, decompressor, or hidden main-RAM pool.
* Integrated fixed-slot residency with compact-model material binding through
  caller-owned preflight arrays. Referenced identifiers are pinned before list
  emission, strip callbacks perform no cache admission, and pins remain held
  until the application releases them after render completion.
* Removed shared mutable scratch matrices from the established 3D transform
  helpers and added a bounded caller-owned matrix stack with explicit
  overflow, underflow, and non-consuming restore operations. It allocates no
  memory and adds no initialization or per-frame cost to applications that do
  not use it. Added failure-atomic perspective and look-at builders with
  explicit descriptors, plus a two-input memory matrix composition routine
  whose order matches the established post-multiply API. The new stack,
  composition, and camera operations are available to loadable modules through
  the established Dreamcast architecture export table.
* Added allocation-free PVR geometry projection over bounded strided canonical
  vertex streams, with prefix-safe error reporting and caller-owned memory,
  current-list, or explicit buffered-list sinks. The sinks preserve existing
  direct and vertex-DMA submission while leaving scene ownership unchanged.
  Format-aware projection and sinks also cover complete 32-byte untextured and
  64-byte textured two-volume vertices without changing the canonical sink
  ABI; command and XYZ fields are transformed while both volume attribute sets
  are preserved exactly.
* Added checked, allocation-free sprite-cell geometry over the established
  textured PVR sprite packet. Caller-owned atlas cells and strided instances
  support normalized pivots and UV regions, independent scale and rotation,
  UV flips, visibility compaction, screen-space output, and projected 3D
  billboards. Existing checked materials and geometry sinks retain texture,
  color, scene, list, and submission ownership.
* Added allocation-free cell-sprite composition and timestamped step streams.
  Ordered stream lists independently override atlas selection, local transform,
  signed priority, visibility, material routing, and per-corner colors before
  one whole-sprite transform is applied. Resolved cells feed either the compact
  hardware sprite path or expressive four-corner colored strips in 2D and 3D.
  Generic animated transforms bind directly to whole sprites, and bounded
  event traversal follows each stream's independent repeat time base.
  Dreamcast rotations, quaternion extraction, and projection use SH4ZAM;
  callers retain every clock, state array, workspace, texture, material, scene,
  and submission object.
* Added bounded caller-owned particles with failure-atomic pool clearing,
  spawning, and deterministic constant-acceleration stepping. Active particles
  can compact into the sprite-cell path, expand into colored or textured
  polygon billboards, or form ordered camera-facing trails through established
  checked geometry sinks. The API creates no allocator, worker, timer, random
  source, texture, material, scene, or list ownership.
* Added validated caller-owned compact-model texture tables with sorted
  13-bit identifiers, palette selection, checked surface/VRAM admission, and
  allocation-free binary lookup. A material adapter now resolves persistent
  compact draw state into existing one- or two-volume KOS materials and submits
  them through established current or buffered list paths without owning a
  texture namespace, asset lifetime, scene, list, allocator, or worker.
* Added contiguous caller-owned PVR memory reservations and failure-atomic
  multi-surface layout planning. Checked slices bind existing texture-surface
  descriptors to exact non-owning ranges inside one established allocator
  allocation without introducing a second allocator, global registry, worker,
  or permanent workspace.
* Added bounded compact PVR model streams with allocation-free record
  iteration and whole-model validation. Record framing, per-format vertex
  sizes, finite data, strip and volume structure, counters, and every indexed
  vertex reference are checked before later traversal or rendering consumes
  the model. Typed vertex-batch and strip views now expose bounded entries
  without repeating format arithmetic. Format-neutral decoding expands
  normals, colors, intensities, UV sets, user data, and generic metadata;
  admitted models require unique vertex ranges and provide deterministic
  allocation-free indexed lookup. Parent-before-child model hierarchies
  compose through caller-owned matrix workspace with no recursion or
  allocation. Added bounded one-volume and two-volume strip emission with
  complete support and capacity preflight, decoded primary/secondary render
  state, explicit texture-ID resolution, reversed-winding correction,
  SH4ZAM-backed projection, and existing memory/current-list/buffered-list
  sinks. Two-volume output binds each call to the matching complete 32-byte or
  64-byte TA vertex layout. Added a separate compact modifier-volume pass that
  expands triangle, quad, and strip topology with preserved winding, projects
  all three positions, and publishes each header/triangle pair atomically to a
  modifier list. Compact bump materials now expose their persistent
  signed-normalized direction/up basis to explicit application vertex policy;
  unsupported state families fail before callbacks or output.
* Added a host-side compact-model inspector that decodes explicit
  little-endian vertex and polygon streams, applies the exact target runtime
  validator, and reports deterministic topology, indexing, strip, and texture
  statistics. It defines no additional container format, and malformed assets
  can now be rejected during a host build before being linked or packaged.
* Added a deterministic host-side OBJ converter for finite indexed positions,
  per-corner signed fixed-point UVs, normalized signed-16-bit normals, and
  explicitly triangulated faces. It preserves independent OBJ attribute
  indices without duplicating positions, accepts positive and
  relative-negative references, splits vertex and strip records before their
  16-bit fields overflow, applies
  explicit winding/V flips and either one global texture identifier or repeated
  material-name-to-identifier bindings, coalesces aliases, emits persistent
  texture state only on actual transitions, and runs the target validator
  before publishing little-endian output streams. An opt-in strip optimizer
  joins only order-adjacent faces with exact position/UV/normal edge identity,
  honors alternating strip winding and resolved texture boundaries, reports
  the reduction, and splits at both strip and record encoding limits. Explicit
  host-selected material libraries now convert a strict diffuse, ambient,
  specular, and exponent subset into persistent compact render state, with
  deterministic quantization and no implicit file access. A generated-C mode
  embeds both naturally aligned streams and exact calculated bounds behind one
  immutable `pvr_chunk_model_t`, ready for the normal target compiler without
  invoking a compiler from the converter. Validated model metadata now exposes
  the largest strip vertex count so callers can size renderer workspace without
  rescanning the polygon stream. The compact-resource example builds its model
  through this complete conversion, optimization, embedding, residency,
  material, and rendering path. Opacity, texture-map paths, source
  triangulation, and global topology reordering remain explicit rather than
  being guessed.
* Added allocation-free CPU vertex-lighting kernels with checked
  inverse-transpose normal matrices, bounded strided normal transforms,
  directional and attenuated point Lambert lights, and deterministic saturated
  ARGB packing. Dreamcast vector math uses SH4ZAM while host validation retains
  a portable scalar path.
* Added format-neutral, allocation-free keyframe sampling with validated
  immutable scalar, vector, and quaternion track views, logarithmic clamped
  interval lookup, shortest-path rotation interpolation, fallback-channel
  object sampling, blended TRS transforms, and explicit local-matrix
  publication. Dreamcast interpolation and rotation paths use SH4ZAM without
  changing XMTRX.
* Added admitted transform clips and caller-owned one-shot, loop, and ping-pong
  playback cursors with constant-time boundary traversal. Sampled local
  matrices bind directly to compact-model hierarchies, including exact
  in-place composition, while camera and light tracks publish through the
  established checked matrix and `pvr_light_t` contracts. The facility creates
  no clock, worker, fiber, scene manager, or hidden pose allocation.
* Added step-only visibility tracks, strictly ordered application event markers,
  and scalar morph-weight bindings. Event collection follows forward, backward,
  loop, and ping-pong traversal, supports zero-capacity counting, and bounds
  publication by caller capacity even across very large loop counts. Morph
  output uses the existing `pvr_morph_target_t` and deformation kernel directly.
* Added bounded, allocation-free additive morph-target deformation and indexed
  four-influence linear-blend skinning over caller-owned streams and joint
  palettes. Complete structural, influence, and palette validation precedes
  skin output; exact canonical in-place operation is supported; partial morph
  failures report a valid prefix. Dreamcast transforms and normalization use
  SH4ZAM without changing XMTRX.
* Added renderer-independent, allocation-free collision geometry for rays,
  triangles, unit planes, finite segments, spheres, capsules, axis-aligned
  boxes, and oriented boxes. Checked closest-point, projection, ray interval,
  barycentric hit, inclusive overlap, separating-axis, and bounds operations
  retain no world or object state, normalize published point/vector W
  components, and leave caller output unchanged on failure. A bounded strided
  point stream can produce AABBs without an intermediate mesh copy. Dreamcast
  dot products and magnitudes use SH-4 vector instructions while host tests use
  the portable path from the same source.
* Added immediate-admission asynchronous PVR texture transfers with coherent
  progress, timed waits, and terminal request ownership. Added checked
  YUV420/YUV422 macroblock input sizing and conversion whose request completes
  only after both channel-2 DMA and converter completion interrupts. These
  opt-in operations create no worker, queue, permanent buffer, or idle thread.
  Legacy nonblocking image DMA now retains shared channel ownership until its
  completion interrupt instead of admitting scene-list DMA prematurely.
* Added checked PVR color-clamp endpoints, punch-through alpha threshold,
  bounded bulk palette writes, and vertex-buffer assignment. Extended polygon,
  sprite, and two-volume header compilation exposes texture supersampling
  without enlarging established public context structures or adding work to
  the legacy compiler path.
* PVR initialization now validates the complete vertex, object-list, region,
  and frame-buffer layout before clearing VRAM or changing hardware state.
  Invalid bin sizes, overflow, frame-bank exhaustion, and failure to register
  the required VBlank callback fail cleanly. Checked render-to-texture entry
  now rejects active scenes, invalid VRAM aliases, misalignment, arithmetic
  overflow, and pitched targets that extend beyond PVR RAM.
* Added opt-in direct and buffered PVR multipass registration for one through
  eight passes. Each pass has independent bins, translucent sorting, and DMA
  staging, while hardware continuation preserves shared parameters, depth, and
  tile accumulation until one final renderer submission. The established
  one-pass path retains its ABI, allocation model, and DMA behavior. Pass-aware
  hybrid flushing switches only the opted-in scene to lockstep continuation.
* Added allocation-free PVR render tickets with monotonic scene identities and
  identity-specific queued, registered, rendering, complete, and displayed
  stages. Texture tickets describe their exact target geometry, reject display
  waits, and make the render-to-texture reuse boundary explicit; framebuffer
  tickets separately identify the VBlank at which their result becomes visible.
* Completed the established extended texture loader with checked dimensions,
  honest preformatted-copy behavior, bounded transfer selection, and an
  error-reporting entry point while preserving the original void wrapper.
* Added an opt-in post-boot direct GD-ROM transport with bounded packet PIO and
  DMA, status, geometry, mode, recovery, CDDA, streaming, diagnostics, and
  asynchronous request integration. Boot-time disc authorization remains the
  responsibility of the firmware.
* Added queued asynchronous CD requests with cancellation, deadlines, progress,
  terminal sense data, finalizers, and callbacks dispatched outside IRQ and
  transport-worker context. Request and callback workers are created on first
  use.
* Added typed TMU channel ownership and opt-in software timer events, together
  with caller-stacked cooperative fibers, synchronization objects, and explicit
  service executors. Ordinary threads carry no fiber state or stack allocation;
  service wake/message delivery cannot falsely complete a mutex or event wait.
* Added validated flash configuration and play-history transactions, full-range
  RTC counter/calendar conversion, checked cable-aware video-mode policy,
  coherent SCIF configuration/status, and side-effect-bounded expansion-device
  discovery. Physical writes, timing, and device variants remain validation
  gates.
* Extended ISO9660 with selectable BIOS/direct transport, aligned-sector and
  arbitrary-byte asynchronous reads, preseek, staged streaming, media
  invalidation, directory prefetch, and cache statistics. The 32 KiB bounce
  workspace is allocated only when an unaligned byte read needs it.
* Added bounded sector-range handles and typed CDDA, media recognition,
  drive-state, disc-identity, and media-event facilities.
* Added encoding-aware BIOS-font glyph lookup and corrected ASCII space to use
  the historical blank replacement glyph instead of the overbar slot.
* Added stored-order Maple function-descriptor and connection-direction helpers,
  coherent controller snapshots and transitions, analog-trigger hysteresis,
  capability-aware soft-reset detection, and strict response-size validation.
* Completed keyboard metadata and coherent state snapshots, bounded both legacy
  key queues, synchronized callback configuration, and corrected attach-time
  status clearing that could overwrite memory beyond the keyboard state.
* Completed mouse condition decoding with eight buttons, eight axes, overflow
  and option data, descriptor metadata, coherent transitions, and safe handling
  of malformed responses while preserving the established leading status
  layout.
* Added response-aware microphone controls and caller-buffered capture rings
  with independent readers, bounded copy-out, seeking, and exact overrun
  accounting. Added per-port stored-camera-image requests with bounded
  deadlines, validated replies, coherent progress, and detach-safe late
  completion. Live camera video remains separately scoped.
* Hardened low-level sound output with validated command queues, sound-RAM
  geometry and allocator status, failure-atomic stream allocation, bounded
  raw/WAV effect loading, exact SPU byte-range copies, and coherent channel
  status. Codecs, sequencing, and content middleware remain separate libraries.
* Added scheduled light-gun capture with exclusive Maple field ownership,
  automatic flash restoration, coherent aim snapshots, port selection, and
  optional IRQ-context trigger and completion callbacks.
* Extended vibration support with typed effect encoding, multi-unit output,
  device and unit metadata, relative orientation, hardware auto-stop, readiness,
  coherent asynchronous completion status, and optional IRQ-context callbacks.
* Extended VMU display support with descriptor-based capability checks,
  top-left grayscale packing into the peripheral's raw bitmap order, relative
  orientation, coherent completion state, and optional IRQ-context callbacks.
* Added validated civil-time VMU clock APIs with synchronous and asynchronous
  reads and writes, coherent completion state, and correct Sunday-zero weekday
  conversion while retaining the established time-based entry points.
* Added host-usable VMU filesystem metadata definitions and whole-filesystem
  validation for root geometry, exact FAT chains, cycles, cross-links,
  duplicate names, orphan blocks, and executable-eligible free space. Normal
  VMU setup now rejects unsafe geometry before allocating or reading metadata.
* Added direct file-metadata queries and geometry-aware full-file and bounded
  block-range VMU reads that resolve and validate an exact FAT chain before
  touching the destination. Raw block reads now reject malformed response
  lengths before copying. Hardened VMU package construction and parsing against
  unaligned input, truncated layouts, integer overflow, unterminated text
  fields, invalid icon metadata, and checksum mismatch without modifying the
  caller's encoded buffer.
* Added synchronous and lazy asynchronous VMU volume inspection that
  distinguishes ready, orphan-degraded, unformatted, corrupt, and unsupported
  filesystems while reporting directory usage and both ordinary and
  executable-eligible free space.
* VMU saves and deletions now gate all mutations on whole-filesystem ownership
  validation. New saves use data/FAT/directory commit ordering, replacements
  use copy-on-write, deletions remove the directory entry before freeing data,
  failed allocations leave the in-memory FAT unchanged, and executable images
  require contiguous free space beginning at block zero. A high-level query
  now reports that executable-eligible prefix separately from total free space.
* Added validated VMU file renaming, including `/vmu` VFS rename support, and
  directory-only updates for copy protection and file-header offset. Existing
  rename destinations are removed before their blocks are reclaimed, so an
  interrupted replacement can leak space but cannot cross-link live files.
* Added synchronous and lazy asynchronous multi-file deletion with complete
  preflight, directory-before-FAT ordering, confirmed-removal accounting, and
  explicit reporting when an unacknowledged directory write makes the exact
  on-card result uncertain.
* Added synchronous and lazy asynchronous bounded VMU file rewrites. Normal
  files preserve all directory metadata through a copy-on-write chain switch;
  executable files use pre-read rollback because their required block-zero
  placement prevents dual-chain publication. Added validated orphan-only FAT
  repair that never reclaims a block reachable from a live directory entry.
* Added validated bank discovery, selection, and lock control for compatible
  multi-bank memory cards, together with lazy asynchronous VMUFS requests and
  host-tested command/response vectors. Physical multi-bank hardware remains
  a required validation gate.
* The `/vmu` VFS now distinguishes logical package payload length from
  block-rounded backing storage. Reads, seeks, totals, `stat()`, `fstat()`,
  append writes, sparse gaps, and package rebuilds use the logical EOF, while
  `O_META` continues to expose the complete stored image. Unmodified writable
  handles no longer rewrite a card merely because they were closed.
* Added quick and full standard-card formatting with invalid-root-first commit
  ordering and final metadata verification. Added copy-on-write
  defragmentation that packs ordinary files high, retains an executable at
  block zero, safely stages relocation cycles, and refuses before mutation
  when insufficient scratch space prevents an interruption-safe schedule.
* Added lazy asynchronous VMU range-read, save, delete, rename, attribute,
  format, and defragment requests with coherent block-level progress,
  cancellation at transaction-safe boundaries, and application callbacks
  dispatched outside the storage worker and IRQ context. Save data is read
  back before allocation metadata can make its chain visible.

RELEASE NOTES for 2.2.2
-----------------------

Another minor patch version to collect ~20 PRs of bug fixes. Notably GDB is no
longer built by default with the toolchain as it was frequently breaking,
further improvements to rumble support, and fixes to allow CLion to function
again with our build wrappers.

RELEASE NOTES for 2.2.1
-----------------------

This minor patch version is primarily aimed at fixing a bug in newlib that
caused serious issues with dma reading. Alongside that ~20 PRs were included that
contained minor bugfixes and cmake environment updates. Notably, issues reported
with clock drift from the DC's SH4 clock should be fixed and 1st party rumble
packs are now supported.

RELEASE NOTES for 2.2.0
-----------------------

# What's New in Version 2.2.0

There are three major changes in v2.2.0 compared to prior releases. The first
is that it didn't take 10 years! Other than that, we have a new implementation
of [POSIX threading](./RELNOTES.md#libpthreads) and have updated the default
[Floating-point ABI](./RELNOTES.md#floating-point-ABI) to `m4-single`. A more 
thorough explanation of each can be found below the rest of the high-level
changes.

# Core Functionality
* FS: `.` and `..` handling and `stat` for all vfs.
* Expanded `KOS_INIT_FLAG` options for vfs support.
* New Priority Boosting scheme for threads.
* Fixed library loading functionality.
* Added `getpeername()`, `settimeofday()`, and expanded `sysconf()` support.
* Entirely new libpthreads providing enhanced support for POSIX threading.

# Dreamcast Functionality
* Support for CD IRQ-based DMA, and DMA Streaming.
* Reworked APIs with strong typing for: IRQ, DMAC, bfont, Keyboard, and PVR.
* Support for SQs when using MMU.
* New Performance counter based performance monitor API.
* VMU metadata is now excluded from standard file reading and managed by API.
* [Driver for the SCI interface. Accessible on DC via mod or NAOMI via CN1.](https://github.com/KallistiOS/KallistiOS/pull/978)

# API Breaking Changes

## Strict types breaks

In an effort to improve the reliability of our API overall, we have been moving
to using enum types and other defined types in place of standard types. This has
been done notably in the bfont, Keyboard, and PVR API reworks. These should
all remain compatible with previous implementations, but gcc 14+ will mark many
as errors rather than warnings. In every case, the error should clearly show
how simply changing the types of parameters will correct them.

## VMU file headers

Reads and writes to `/vmu/` will no longer need to be manually adjusted to avoid
writing into the file header or managing it specifically with file operations.
Instead `fs_vmu_set_header()` can be used to set the header to be used for a file
and `fs_vmu_default_header()` can be used to set a header to be used by default
for all files written to vmus. The file header contains metadata used by the
Dreamcast's BIOS in its memory manager and contain text and images.

Prior to this change, files created by KOS on memory cards would either show
garbage in the Dreamcast's BIOS or the header would need to be manually compiled
and written to files prior to writing other data. This old behavior can still
be accessed by opening a `/vmu/` file with the `O_META` flag in `fs_open()`.

# libpthreads

POSIX threading (pthreads) support has been moved out of the kernel and into
its own addon library (libpthread). This support has been vastly improved and is
much more complete and standard-compliant than it was before. It still isn't
100% POSIX-compliant by any means, but it's a lot closer than it was. There is
no guarantee that this will work with GCC's `--enable-threads=posix`, as that
configuration is not tested/supported any longer in dc-chain.

# Floating-point ABI

A significant change has been made regarding the default floating-point ABI used
by KallistiOS. In previous KOS releases, and even in commercial games released
during the Dreamcast's lifetime, the `m4-single-only` floating-point ABI was
used. With the `m4-single-only` ABI, the SH4 CPU is always in single-precision
mode and all uses of 64-bit `double` values are truncated to 32-bit `float`
values by the compiler, allowing the compiler to use twice the number of
floating-point registers at the expense of precision. Going forward, KOS now
uses the `m4-single` floating-point ABI by default. With the `m4-single` ABI,
the SH4 CPU is in single-precision mode upon function entry, but the compiler
can change the mode and use true 64-bit `double` values within functions. For
most projects, there are no implications from this change other than gaining
the ability to use 64-bit `double` values. There are, however, two possibilities
for negative effects:

- In older projects using `double` values (which were actually being truncated
  to 32-bit `float` values upon compilation anyway), the code should be changed
  to explicitly use `float` values instead. If not changed, fewer floating-point
  registers may be available to the compiler, in exchange for a needless bonus
  doubling of floating-point precision.

- The order of floating-point register names is changed. When using
  `m4-single-only`, register names are ordered fr4, fr5, fr6, fr7, etc., but in
  `m4-single`, register names are ordered fr5, fr4, fr7, fr6, etc. In order to
  account for this difference and still have inline assembly functions using
  floating-point registers work properly regardless of the ABI used, the
  KOS_FPARG(n) macro has been provided in `arch/args.h`.

Despite the change to `m4-single` by default, KOS is still committed to full
support for `m4-single-only` in addition to offering new support for
`m4-single`. This is selectable using the `KOS_SH4_PRECISION` environment
variable within environ.sh. It is highly recommended to compile KOS, all
kos-ports, and all libraries with the same uniform setting as your projects.
Other ABIs, such as `m4` or `m4-nofpu`, are not supported at this time.


RELEASE NOTES for 2.1.1
-----------------------

This minor patch version is primarily aimed at fixing the versioning system
which simply didn't work as implemented in v2.1.0. Alongside that another few
dozen PRs were included that containing minor bugfixes and documentation updates.

Also included is a new host-side util pvrtex which converts standard images
to formats used directly by the Dreamcast's PowerVR (utils/pvrtex), a significant
rewrite of wav2adpcm which converts standard sound data into the smaller ADPCM
format used by the Dreamcast's AICA (utils/wav2adpcm), an example that
demonstrates how to draw lines with quads via the pvr (pvr/pvrline), one for
testing network speed (network/speedtest) and another on how to use libADX
from kos-ports for audio playback (sound/libADX).

RELEASE NOTES for 2.1.0
-----------------------

# What's New in Version 2.1.0

KOS v2.1.0 has been a long time in the making. As such, it seemed prudent to
provide an overview of the new functionality since v2.0.0 in 2013. We intend
to have more frequent versioned releases moving forward, so this kind of
information should be easily seen in the changelog.

# Core Functionality
* Cooperative Threading mode is no longer supported.
* Static Thread Local Storage (TLS).
* C11 threads and worker threads.
* /dev/ vfs supporting null, random, and urandom.
* VFS Expanded with readlink, rewinddir, and more compliant readdir and stat.
* Expanded C language support including C11, C17, and C23.
* Expanded C++ language support including C++11, C++14, C++17, C++20, C++23, and C++26.
* Expanded POSIX support: clock_gettime/settime/getres, getaddrinfo/freeaddrinfo, libgen.h, and more.
* GCC 9-15 supported. Support for GCC 2-3 removed, and 4 deprecated.
* Default language spec of the codebase is now gnu17/gnu++17.

# Dreamcast Hardware Support
* NAOMI/NAOMI2 including net-dimm uploading.
* New and enhanced driver for SH4 User Break Controller (UBC).
* SH4 Watch Dog Timer (WDT) device.
* Hardware Performance Counters.
* Support for m4, and m4-single modes alongside m4-single-only.
* Store Queue access is now managed by KOS and direct access may break.
* PVR YUV converter DMA.
* PVR 'cheap' shadows via volume modifiers.
* PVR Two-pass render-to-texture option.
* CD-ROM DMA, subcode, and alternative data type reading.
* 4/8-bit wav support for sfx and streaming audio.

## Peripherals and Accessory Support
* French AZERTY, German, Spanish, and UK keyboards.
* Basic Lightgun support based on libronin's implementation.
* VMU buttons, date/time, BIOS color, and using the 'extra 41 blocks'.
* Enhanced support for testing the capabilities of connected controllers.

## Hardware Modification Support
* Additional G1 ATA device (IDE hard drive mod).
* 32MB RAM upgrade.
* Custom BIOSes.
* Navi modified Dreamcast subarch has been moved to addons.

Below are more verbose notes for some of the changes
-----------------------
There are a lot less major changes in this release than in the previous one,
that is for sure. Of course, this isn't to say that there hasn't been some
interesting changes along the way.

The first change is that all targets deprecated in 2.0.0 were removed entirely
from the tree. That is to say, there are no remnants of the GBA, PS2, or ia32
ports of KOS in the tree anymore. If someone REALLY wants them back, please let
me know at some point and we can work that out. I doubt this will come up at
all, however. In addition, the navi subarch was moved out of the main tree and
into the libnavi addon library.

Further standards compliance issues were worked out for this release. KOS' core
should now compile cleanly with a relatively new GCC with the -std=c99 flag
(as well as -Wall and -Wextra). Older (prior to 4.7.x) versions of GCC might
complain about one or two things here and there, but that is not of any
particular concern.

The fs_stat() function has been completely reworked to actually map cleanly onto
the normal C stat() function. This means you must use a struct stat when calling
the function, rather than the removed stat_t (you'll get an error if you try to
use the old struct, since it is completely gone now). If I recall correctly, the
only filesystem to actually have any direct support for fs_stat() before was
fs_vmu (which only supported it in a strange manner to get the free space left
on the VMU in question). The fs_vmu behavior has been retained so that if you do
something like fs_stat("/vmu/a1", &buf, 0), you will still get the number of
free blocks in buf.st_size (the standard doesn't say what to do with the st_size
value on a stat() call about a directory, so this is actually compliant with the
standard, oddly enough). More filesystems support fs_stat() directly now, such
as fs_dcload/fs_dclsocket. Also added is an fs_fstat() call (which, of course,
maps onto the normal C fstat() function). If a filesystem doesn't support
fs_fstat(), it will get a simple mapped version of it, much like with fs_stat().

Hardware-wise, a new driver was added for accessing a hard drive that might be
hooked directly up to the GD-ROM port. The GD-ROM itself is actually a bit of a
strange ATA device, and it is entirely possible to chain a slave device off the
connector with a bit of a hardware modification. The new driver is in g1ata.c
(in the kernel/arch/dreamcast/hardware directory), and should work relatively
well with devices that comply with the ATA standard. The driver supports both
PIO and Multi-word DMA based access to the hard drive device, and can vastly
surpass the potential speed of the GD-ROM itself (with DMA, I get around 12.5
megabytes per second reading sequential sectors off the drive in testing). There
have been a few small modifications to the cdrom.c file to accommodate the
possibility that a device other than the GD-ROM drive might be selected on the
bus (the BIOS syscalls do not check what device is selected). GD-ROM access is
still done through the BIOS syscalls for various reasons (including dcload ISO
redirection, which relies on the syscalls being used). The hard drive access
layer exports a kos_blockdev_t interface to interact with the drive, so you
should be able to use libkosext2fs with hard drives without any difficulties.

The microphone driver (for the Seaman mic) has been changed around a bit. The
internal buffer has been removed in favor of a callback-based sampling approach.
The callback will be called each frame (in an IRQ handler context) while
sampling with the samples collected that frame. The idea is that you'd copy the
samples into some buffer in your program and basically return immediately from
the callback (it is called in an IRQ handler context, so you really shouldn't be
doing a lot of work in the callback).

The kos-ports tree has been changed quite a bit from the last release. No longer
does the kos-ports tree itself host all the source code of the various libraries
it contains, but rather just (generally) a Makefile with some metadata and any
patched or additional files needed for building the library for KOS. You build a
library simply by going into its directory in the kos-ports tree and running
"make install clean", much like the FreeBSD ports collection. Check out the
README in the kos-ports tree for more information. Some libraries have been
updated in the switchover, so keep that in mind. Anything that uses Lua or
liboggvorbisplay will probably have a bit of fixing up needed. Lua has been
updated to 5.3.0. liboggvorbisplay has been split into three libraries: libogg,
libvorbis, and liboggvorbisplay (the first two are the official libraries from
Xiph.org and liboggvorbisplay is the KOS wrapper for them).

Also, speaking of kos-ports, SDL has been updated a bit to version 1.2.15. If
anyone is interested in updating SDL further to the 2.x versions, feel free to
contact me. As of now, nobody's maintaining the KOS port of SDL, so it could use
a maintainer too. Continuing on with kos-ports news, a port of the Opus audio
codec (the successor to Vorbis and Speex) has been added. Like the Vorbis port,
this one is split into multiple libraries, mirroring how it is distributed. Opus
and Opusfile are direct from Xiph.org. The libopusplay library links against
those two libraries and adds in the KOS-specific interface, which should look
very familiar to anyone who's used liboggvorbisplay.

A new VFS driver for accessing FAT filesystems was added in libkosfat in the
addons tree. This driver supports FAT12, FAT16, and FAT32, including proper
long filename support. FAT isn't quite as robust of a filesystem as ext2 is,
but it is probably a lot easier to work with on SD cards, considering how
widely supported FAT is on pretty much every OS ever.

Basic support has been added for getting things up and running on a NAOMI or
NAOMI 2 arcade board (since both are variants of the Dreamcast hardware). In
order to do this, build KOS with the KOS_SUBARCH set to "naomi". Support is
fairly basic at the moment, but will hopefully improve over time (if I can get
a hold of working hardware again). Two utilities have been added to the utils/
tree for network bootable NAOMI binaries and for actually network booting them,
assuming you have the requisite hardware.

Support was added for various mods that have been made for the Dreamcast
hardware in recent years. Support for ATA devices on G1 was already mentioned
up above, but also added was support for modified BIOSes (necessitating a
change to the GD-ROM initialization code), and more interestingly for consoles
with 32MiB of RAM.

Support has been removed for using toolchains with GCC 3.x and older. Going
forward, at least GCC 4.7.4 is required for building KOS. The GCC patches for
4.x improved/cleaned up building with KOS a lot, and I doubt there's many good
reasons to keep around support for the old patches with GCC 3.x.


RELEASE NOTES for 2.0.0
-----------------------
This release has been a long time coming, to say the least. Pretty much every
part of KOS has been modified in some way since 1.2.0 and many things have
undergone a complete overhaul. After almost a decade of living exclusively in
the source repository things have finally settled to a point where a release is
possible and a good idea.

All of the various platform targets for KOS other than the Dreamcast target are
considered deprecated unless someone else steps up to maintain them. If nobody
steps up, these will be removed at a later date. I somewhat doubt that any of
the other platforms can be built successfully anymore at this point.

Several libc standards compliance things were fixed, so stdlib.h no longer
includes assert.h for you. This will break some code that assumes that
assert() is available when stdlib.h (or kos.h) is included.

Speaking of libc-related standards compliance stuff, the built-in libc has been
removed entirely in favor of using Newlib directly. You must build a patched
Newlib for use with KOS. The patches needed for various versions of Newlib are
included in the utils directory of the KOS source.

The build system (including environ.sh) has seen some overhauling. You'll
need to build a new environ.sh from a sample again. Additionally, your
Makefile may need to change. See the examples.

The sound stream system has changed to accommodate multiple streams. Please see
kernel/arch/dreamcast/include/dc/sound/* for the new info. In particular, you
will need to call snd_stream_init from your program before using any of the
libraries like OggVorbis. Also if you are a stream user, you need to alloc
and free channels, and pass a handle along with it. See the OggVorbis libs
for an example.

The dbgio functions have changed. It now implements a full debug-friendly
console system. See include/kos/dbgio.h for more info.

There is a new build system for addons/ports which is quite a bit more
automated than the old way, and is arch-centric. Now to build a new addon
you downloaded, just extract it into addons/ and it will be built for
your arch if possible.

Subsequently, most everything that was in addons/ in previous versions of
KOS is now located in its own source control tree (and will be distributed
separately).

Several incorrectly placed pieces were moved from kernel/ into their own
addon (libkosutils). If you use the bspline or kos_img_* functions, you'll
need to add -lkosutils to your link line somewhere.

KOS now has a built-in network stack in the kernel/net directory. This is only
usable at the moment with the Broadband Adapter or the Lan Adapter for the
Dreamcast. It supports UDP and TCP over both IPv4 and IPv6. You also have an
almost complete set of sockets functions so that you can use the networking
support just like you would on any other OS. Add INIT_NET to your KOS_INIT_FLAGS
to initialize the network support on startup.

If you are using the networking support on the Dreamcast, it is now possible to
use dcload debugging through KOS' network stack. This is automatically set up
for you if you initialize the network at startup time (through the
KOS_INIT_FLAGS).

The old Maple API (that was deprecated in 1.1.8) has been removed entirely from
the source tree. Please update any code that may still be using it to the "new"
API that has been around since 1.1.8.

There are a few new Maple drivers included with this release that were not
around previously. There is now support for the PuruPuru pack, the Dreameye
camera, and the Sound Input Peripheral. See the appropriate header files for
more information about how to use each of these.

All of the KOS header files have had Doxygen comments added to them. From now
on, anything to be added to KOS should be documented before it is included in
the main tree.

A lot of cobwebs have built up over the years in the KOS source, and its quite
possible that there are parts of the code that do not work properly anymore.
Please, if you find anything that doesn't work properly anymore, report an issue
on the Sourceforge bug tracker!

Many changes were made to the synchronization primitives and other threading-
related things. Make sure to take a look at the documentation on them.

Support for the homebrew serial port SD card reader has been added. There is a
block-level driver for SD cards, as well as a lower-level driver for using the
serial port as a generic SPI bus.

If you would like a ready-to-use filesystem to go with your SD card support,
look no farther than libkosext2fs (in the addons tree). This provides support
for reading and writing to ext2 filesystems. This library is still pretty raw
and could use a lot more testing, but it seems to work fairly well as long as
you respect the few limitations in it (no files >= 4GiB in size being the big
one, absolute paths in symlinks don't work either). As a result of adding this
filesystem into the tree, there were a bunch of other changes made in the VFS
code. Specifically, fs_link() and fs_symlink() were added.

The GBA, ia32, and PS2 ports of KOS are all considered abandoned and are likely
to be removed in the future. If you would like to step up to maintain/improve
them, please let us know!

There's probably a whole bunch of other stuff that should be mentioned in here,
but its been so long since anyone has updated this document...


RELEASE NOTES for 1.2.0
-----------------------
The PVR API's performance/statistics measuring facility has changed.
Rather than try to keep backwards compatibility, the new structs have
been changed so that the names are more accurate. The main change that
will be user-noticable is that "frame_count" has become "vbl_count",
counting the number of VBlanks, which is a much more useful measurement
(so you can do constant rate animations and such).


RELEASE NOTES for 1.1.9
-----------------------
The snd_sfx_* API has changed to allow for unlimited numbers of sound
effects. The main difference is that you now use sfxhnd_t instead of int
for addressing sound effects, and the invalid return value for snd_sfx_load
failure is 0/NULL/SFXHND_INVALID and not -1.

libdcutils has been removed at this point. Everything that was in it has
been moved elsewhere or just removed in general. This includes:
  - 3dutils -- moved into kernel/arch/dreamcast/math
  - bspline -- moved into kernel/misc
  - matrix -- moved into kernel/arch/dreamcast/math
  - pcx.c -- moved into addons/libpcx
  - pcx_texture.c -- ditto
  - precompiler.c -- removed
  - pvrutils.c -- redundant, removed
  - rand.c -- removed; a different randnum() is in libc now
  - sintab.h -- removed
  - tga.c -- moved into addons/libtga
  - tga_texture.c -- ditto
  - vmu.c -- merged into kernel/arch/dreacmast/hardware/vmu/vmu.c

The GBA code base should be functional again. I've sync'd in a bunch of
changed from Gil Megidish which brings everything relatively back up to
date:
  - math.h: GBA now supported, and include/newlib-libm-arm
  - lua and lwip not compiled for GBA
  - support for romdisk and initflags for GBA
  - mockups for threading/irq for now
  - pogo-keen example

The thread scheduling system has been changed up slightly, though this
shouldn't affect most users. If you call thd_schedule or thd_add_to_runnable,
then you should probably look at the notes in kernel/thread/thread.c above
thd_schedule.

The fake "thd_enabled" has been removed completely at this point. If you
had code which checked it (it used to resolve to "1") then you should
go ahead and remove those "if" statements. The closest thing at this point
would be thd_mode == THD_MODE_COOP.

A very early port to the PS2 RTE has been added to the source tree, but
will not be released as binaries (not mature enough yet). If anyone plays
with this or has fixes, I'd very much like to hear from you.

The SYSCALL macro was _very_ broken, as in "I'm surprised it works"
magnitude of broken. This may be responsible for some of the apparent
breakage with newer compilers.


RELEASE NOTES for 1.1.8
-----------------------
There is now a new public maple API. Please see the examples for how to
use this new API. It's pretty similar to the old API except that you call
different functions to get the info, and some of the data interpretation
has changed since the last version. Specifically, controller buttons are
no longer inverted and the joystick is centered at 0 like one would expect.

If you used to use vmu_icon_init in libdcutils, it has been replaced by
vmu_set_icon. The new one will target all attached VMUs.

The sound API (higher level one with streaming and such) now has a real
allocation system for SPU RAM. This means that, like the change earlier
in TA->PVR, you can no longer just assume that SPU RAM is free to trample
on, nor can you assume that resetting the stream driver will release your
samples. You have two options here: you can use snd_sfx_unload to unload
a single sample loaded with snd_sfx_load; or you can use snd_sfx_unload_all
to unload all loaded samples at a shot. Note that unlike previous versions,
this will not touch other samples you may have allocated (or streaming
buffers) so these must be done separately. Calling snd_init() will reset
all SPU allocation.

The sound stream API has changed quite a bit internally, but the main
external change is that the "more data" callback now returns not only a
block of data, but the amount of data.

The deprecated TA API has been removed entirely. You need to convert any
remaining code to the new PVR API or KGL. You can take a look at the
examples to see how this works, but here is a quick rundown:
   - poly_hdr_t becomes pvr_poly_hdr_t
   - ta_poly_hdr_txr becomes pvr_poly_cxt_txr
   - ta_poly_hdr_col becomes pvr_poly_cxt_col
   - pvr_poly_compile must be called to generate the actual pvr_poly_hdr_t
   - ta_commit_vertex and ta_commit_poly_hdr become pvr_prim
   - TA_VERTEX_NORMAL becomes PVR_CMD_VERTEX
   - TA_VERETX_EOL becomes PVR_CMD_VERTEX_EOL
   - TA_ARGB4444 (and others) become PVR_TXRFMT_ARGB4444 or whatnot
   - TA_NO_FILTER becomes PVR_FILTER_NONE
   - TA_BILINEAER_FILTER becomes PVR_FILTER_BILINEAR
   - TA_OPAQUE becomes PVR_LIST_OP_POLY
   - TA_TRANSLUCENT becomes PVR_LIST_TR_POLY
   - "uint32 texture" becomes "pvr_ptr_t texture"
   - ta_txr_allocate becomes pvr_mem_malloc
   - Textures must be freed with pvr_mem_free
...and so forth. Most of the API changes are cosmetic, but it's important
to pay attention and make sure you understand the shifts in paradigm where
appropriate as well (such as raw texture space to managed, allocated
texture space; commit_eol gives way to real lists; etc).

The "scene idiom" has also changed to the following:
  pvr_wait_ready();
  pvr_scene_begin();
  pvr_list_begin(PVR_LIST_OP_POLY)
    /* Do your opaque rendering */
  pvr_list_finish();
  pvr_list_begin(PVR_LIST_TR_POLY)
    /* Do your translucent rendering */
  pvr_list_finish();
  pvr_scene_finish();

Deprecated kos_init_all and kos_shutdown_all have been removed.

Deprecated compat macros like ALL_ENABLE have been removed.


RELEASE NOTES for 1.1.7
-----------------------
KOS 1.1.7 is probably one of the biggest, nastiest upgrades KOS has seen
since the 1.0.x -> 1.1.x transition. Unlike that transition =) this one
brings many fixes and helpful features. Most things will continue working
just fine, but specific issues are listed below. Please check through
this if you have problems.

Initialization has changed somewhat. Now instead of calling kos_init_all(),
you will need to use one or more of the KOS_INIT_* macros in
arch/arch.h. These include KOS_INIT_FLAGS and KOS_INIT_ROMDISK. Note that
there are new names for the flags to OR together, also. Please check
kernel/arch/dreamcast/include/arch.h for more info, and/or see the
examples.

Threading is also different now. Threading is now always enabled. Now
before you groan and moan at me, there are now two modes of threading
instead of just enable/disable: cooperative and pre-emptive. In
cooperative threading mode, the thread module is active and it is
possible to do things like thd_pass(), use condition variables
between the main program and an IRQ, etc. However, the timer is not
hooked and no pre-emption will occur. If you enable pre-emptive
mode, then this is basically like the old threads-enabled mode.

Note that kos_init_defaults() is now a compatibility shim which will
correct any implicit defaults. However, if you want better control over
this situation, please change your program to use the macros. Also note
that this and other compatibility shims will be removed by the next
release version (i.e., removed in CVS after the tagging).

The build process has changed slightly. The main change is that libc is
in its own tree, and thus has its own include path. If you are using the
KOS Makefile templates, then you should use $(KOS_LIBS) at the end of your
link line (use this in place of -lkallisti -lgcc). You must also add
-I$(KOS_BASE)/libc/include to your CC line if you're using your own
custom Makefiles.

A couple of things have changed in the environ file, though nothing
drastic. Your existing environ should still work, but I recommend at
least adding the KOS_STRIP variable, as well as adding the
-fno-optimize-sibling-calls parameter to KOS_CFLAGS if you haven't built
a fixed GCC 3.0.3.

Libc has been split out of 'kernel' and into its own tree. This is what
triggered the build process change. In the future this will make it very
easy to replace libc with another libc (such as Newlib). Note that libc
is ported to KOS, not the other way around. This is why the libc objects
are still combined into libkallisti.a (easier linking until we have the
installation mode available...)

Libm from Newlib has been integrated into the source tree so that you
no longer have to pull in a separate Newlib binary. This also ensures that
it's compiled with the same compiler flags as the rest of KOS.

The new "PVR" API has completely replaced the old "TA" API. For the near
future, code based on the "TA" API will continue working, through an
adaptation layer. The one thing which really can't be emulated properly
with the adaptation layer is custom memory management (i.e., allocating
your own textures starting at texture address 0). "PVR" texture pointers
are now real SH-4 pointers, so you must allocate them through the 3D
subsystem or you'll get garbage for textures.

The streaming and basic sound effects portions of the MP3 and OGG libraries
has been split out and placed into the kernel now, as an architecture
independent interface. The DC implementation uses a generic AICA driver
which has been improved upon greatly since the last version. This has three
implications for anyone using sound stuff:

1) If you used sfx_* functions, you must now use snd_sfx_*
2) It is now possible to use basic sound effects without loading the MP3
   or OGG libraries
3) You no longer need to include stream.drv in a romdisk; it's built into
   the library itself now

The entire maple system has been replaced. Most things will still work
as before, but one of the most notable changes is that you will no longer
need to pause between polls on the maple bus. This is all handled
automatically in the background. Enumeration is done by using the
maple_enum_* functions (see dc/maple.h) and the way to access the
keyboard matrix and shift states is different also (see dc/maple/keyboard.h).
VMU saving should be considered somewhat "beta" as is the hotswap
capability. We're still working on finding and fixing issues there,
especially with third-party peripherals.

One consequence of this change which you should pay attention to for your
own programs is that maple_first_controller() and friends might conceivably
fail at one point during your program, yet succeed later. So you'll want
to poll for the devices you want before each condition check rather than
when the program starts. For the most part, the examples have been
updated to do this.

KGL has become a lot more OpenGL(tm) compliant. This means, for example,
that the usage of radians has been deprecated in favor of degrees,
images are expected to be loaded inverted, etc. If you program which
previously worked under KGL is having some issues, you should probably
check to see what changed there. Paul has helpfully created a KGL manual
as well, if you are looking for docs.

Image loaders now use the kos_img_t system so that they can be platform
independent and still pass around the data in a convenient format. This
also makes it easier to flip the data when loading it into the PVR RAM
for KGL usage. The loaders for PCX and TGA have additionally been split
out into their own libraries (libpcx, libtga). So you will need to
use -ltga or -lpcx for these in your link line.

Finally, if you do not have a working G++ compiler for your target, then
please comment out the line in environ for KOS_CCPLUS. This will disable
building any C++ targets. Conversely, if you have a working G++, make sure
you have a KOS_CCPLUS line so that all of the libraries and examples will
get built.
