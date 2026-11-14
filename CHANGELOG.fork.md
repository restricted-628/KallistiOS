# KOS Experimental iteration changelog

This complements, and does not replace, upstream `doc/CHANGELOG.md` or Git
history. Start of maintained iteration records: 2026-10-01. Earlier chat
iterations cannot be reconstructed completely from commit messages. The local
control folder contains a generated index of pinned published-branch history;
it is a historical index, not evidence that those changes passed tests.

## 2026-11-13 — AICA bank registry and private music foundations

- Publish the bounded resident bank registry and host ownership/adoption path,
  including malformed-receipt retention, aggregate service budget and quiesce
  admission. Rebuild ARM firmware and its SH-4 embedding together.
- Add private sequence/MIDI cores, bank resolution, timed-note integration and
  callback command/receipt client. These are not selected by live firmware;
  command registration, physical channel pool and memory placement remain held.
- Add original ASQ sequence authoring/admission and DSP instruction, gain,
  delay/reverb/modulation builders. Include the retained SPU/stream lifetime
  regression delivery. No proprietary implementation or resources are imported.
- Current-base host/sanitizer checks, ARM/KOS build, firmware embedding and
  consumer links pass. Sealed and fresh unplayed-bank probes pass both Flycast
  modes. No played-sample retirement, audible DSP, timing or hardware claim;
  the emulator's missing NOFL behavior leaves linear-delay execution unqualified.

## 2026-11-13 — libADX request, format and loader backlog

- Add bounded sector requests with linear or native-joint destinations and a
  caller-owned 16-entry range loader with flow control, pause and checked stop.
  Providers remain borrowed; software completion never retires their DMA or
  closes their descriptors.
- Add bounded PCM WAV, AU and AIFF readers and playback bindings, packed CRI
  WAV4 software conversion and the verified mono-PCM16 SPSD subset. Add prepared
  start gating independently of production pause. Reject unsupported layouts.
- Record platform/UI applicability without success stubs. Rebuild consumers
  for expanded request and playback state. Fresh full host/ASan/UBSan suites,
  strict SH-4 archive build and 24 staged C/C++ consumer links pass.
- Audible output, physical retirement, broader format conformance and complete
  middleware parity remain open; this publishes the frozen parts 12 through 24.

## 2026-11-13 — SH4DER and SH4DOW adapter completion

- Complete caller-owned glow, focus, warp, chromatic aberration, shadow shafts,
  lens, visor, torch/fog, retained trail/TAA, exposure/feedback and cavity-AO
  adapters. Reuse admitted surfaces, cached headers and prepared geometry;
  KOS remains the sole renderer and resource owner.
- Add native Compact posed-skin, limb-shadow, cooked AO and textured-foot
  consumers. No dependency repin or competing vendor renderer is introduced.
- Fresh normal/sanitizer host suites, clean GCC16.2 build and consumer links
  pass. Fourteen fixtures pass in both Flycast modes with explicit RTT readback.
  Physical hardware, performance and exact inversion of clipped/bilinear effects
  remain unqualified; callers retain producer/reader and surface lifetimes.

## 2026-11-13 — SH4ZAM rotation and scalar update

- Advance the pinned upstream dependency from `ad2f8909` to `5b7dfc22`, including
  combined Euler/u16 rotations, optimized quaternion XMTRX init/apply, register
  permutations, scalar fixes and assembly function sections. Refresh the exact
  alignment overlay without changing its edits; preserve upstream negation.
- Document special-value/FPSCR negation differences, typed C++ angle overloads
  and rotation-only extraction. No renderer algorithm, decoder arithmetic,
  fiber-provider selection or global compiler/FFT policy changes.
- Add shared host/native rotation regressions with independent scalar oracles,
  nonidentity inputs, control-mode checks and native bit-pattern permutations.
  Clean GCC16.2 build, focused host/sanitizer suites and consumer/provider links
  pass. Integration and O0 release tests pass in both Flycast execution modes;
  selected exclusive-provider math/service checks pass. Hardware performance
  and unrelated outstanding media/numerical questions remain unqualified.

## 2026-11-13 — Upstream polygon strip configuration

- Merge upstream through `6b1248c4d`, adding per-context strip partition
  selection and the updated SH4ZAM balls example.
- Preserve the fork's six-triangle default in all four polygon context
  initializers and raw polygon command headers. Explicit choices of one, two,
  four or six are encoded by ordinary and two-volume polygon compilers.
- Rebuild libraries and applications together: `pvr_poly_cxt_t` gains a field.
  Compact material copies retain the caller's selection; sprite and modifier
  commands are unchanged. SH4ZAM and SH4DER dependency pins are unchanged.
- Clean GCC16.2.0 build, focused host suites and native consumer links pass.
  Packet encoding and checked Compact scene completion pass in Flycast's
  interpreter and dynarec; these are not hardware or performance results.

## 2026-11-13 — libADX sparse AFS partition overlays

- Add caller-owned overlay links with newest-first sparse range resolution.
  Zero offsets fall back; nonzero empty entries override. The newest directory's
  count bounds visible IDs, and missing new-ID holes return NOT_FOUND safely.
- Preserve selected archive identity and counted metadata lifetimes. Pop,
  different-base replacement, unbind and registry close detach owned links;
  same-base binding preserves overlays. Rebuild consumers for registry growth.
- Full host/ASan/UBSan suites and strict staged SH-4 C/C++ links pass, including
  a10000-step independent layer/reference model. Filename/sector services and
  audible/hardware qualification remain separate; no shared driver changes.

## 2026-11-13 — libADX resident AFS partition metadata

- Add caller-owned AFS directory loading, work-size discovery and a256-ID
  registry. Each step reads at most512 bytes and validates at most64 entries;
  member access waits for whole-table validation. Short reads, WAIT, cancellation
  and capacity failures retain explicit state without publishing partial tables.
- Count registry references before allowing metadata close. Copied member views
  survive directory close but retain their explicit archive-provider lifetime.
  Native byte ranges do not claim the SDK's packed sector ABI or file ownership.
- Full host/ASan/UBSan suites and strict staged SH-4 C/C++ links pass. The private
  SDK AFS fixture's five ranges match independently parsed metadata. No shared
  Storage/Audio change, physical playback or hardware timing claim is included.

## 2026-11-13 — libADX compressed zero-group insertion

- Add bounded whole-group insertion into compressed ring input, preserving
  wrap/mirror accounting and outstanding DATA borrows. FREE borrows and sealed
  input reject insertion; each call writes at most512 compressed bytes.
- Zero residual groups retain predictor history and may produce a PCM transient.
  Callers retain header-length/group-ordering responsibility; output-side sample
  discard and playback-clock adjustment remain separate Audio work.
- Full host/ASan/UBSan suites and strict staged SH-4 C/C++ links pass, including
  all tested ring offsets, mirror extents and carried-history decode cases.
  No physical playback or timing qualification is claimed.

## 2026-11-13 — libADX bounded amplitude analysis

- Add caller-owned mono/stereo peak-window analysis with bounded strided PCM
  input, typed work sizing and forward-only source-frame record selection.
  Manual input backpressures; the optional decoder-filter adapter leaves PCM
  unchanged and reports a sticky fault if analysis storage cannot keep up.
- Separate record timestamps from a qualified audible clock and document the
  baseline format/corpus boundary without claiming full middleware parity.
- Full host/ASan/UBSan suites pass, including peak/window and concurrent queue
  checks. Strict staged SH-4 C/C++ links retain the eight new APIs. No physical
  playback, hardware timing or shared Audio interface change is claimed.

## 2026-11-13 — libADX decoded loop-count queries

- Publish a lock-free decode-side loop count through the PCM pipe and native
  player status. The count records stream rewinds modulo 2^32, not audible
  playback progress; sequences and forward-only joint sources report zero.
- Preserve the count across pause/cancel/terminal states and clear it on source
  rebinding. Rebuild consumers for the enlarged pipe and player-info structures.
- Full host/ASan/UBSan suites and strict staged SH-4 C/C++ links pass. No live
  loop-enable control, audible-clock or physical-hardware qualification is added.

## 2026-11-13 — libADX joint sources in the native player

- Add explicit joint-source constructors for the PCM pipe, control service and
  native player, plus same-format rebinding without another output allocation
  or start. An optional feed callback runs only on the serialized producer;
  consumer, paused, full-pipe and cancelled paths do not feed.
- Preserve checked sealing, first-error handling and separate producer/provider
  retirement. NEED_END now follows the input-wait deadline. Joint sources reject
  unsupported sequence/loop controls instead of claiming implicit seek support.
  Rebuild consumers for enlarged reader/pipe and embedding player state.
- Full host/ASan/UBSan suites and staged SH-4 C/C++ links pass, including actual
  native player code exercised with pthread-backed fiber and Audio mocks. This
  does not qualify audible playback, final hardware fetch or physical timing;
  no shared Audio firmware/driver interface changes are included.

## 2026-11-13 — libADX live joint PCM reader

- Connect compressed-byte joints to the existing strict incremental decoder,
  with one bounded decode step per read and optional producer-side PCM filters.
  Retain whole input chunks until consumed to avoid partial-return allocation
  failures after decoder progress; large queue heads can add backpressure.
- Add explicit input sealing and queue-prefix detach. Empty unsealed input is
  starvation, not EOF; DONE still requires valid framing and sealed exhaustion.
  Cancellation retains borrowed input until checked close. Close discards unread
  admitted tail but never joins providers or frees/resets their storage.
- Fresh full host/ASan/UBSan suites and staged SH-4 C/C++ links pass, including
  97 reader cases/142,328 assertions per lane. Rebuild joint-state consumers.
  Native player binding, seeking/looping and audible/hardware validation remain
  separate from this forward-only reader.

## 2026-11-13 — libADX caller-owned chunk queues

- Extend byte joints with four FIFO lines, caller-owned nodes, whole-chunk
  Separate mode and partial/adjacent-span Join mode. Payload is not copied;
  potentially joined spans must share their original backing allocation.
- Reserve a node per borrow so full rollback remains possible under pool
  pressure. Seed rejects overlapping ranges; partial operations can report
  LIMIT without mutation. Reset forgets references without freeing payload.
  Rebuild consumers for enlarged joint and query structures.
- Full host/ASan/UBSan suites pass, including173,087 queue assertions and
  1,555,979 existing memory/ring assertions per lane. Strict staged SH-4 C/C++
  links pass. Live decoder adaptation and audible/hardware validation remain
  separate; native pool accounting is not CRI ABI compatibility.

## 2026-11-13 — libADX compressed-byte joints

- Add caller-owned memory and ring transports with split/Get/Put/Unget,
  availability queries, synchronous error hooks and checked reset/close.
  Ring wrap storage mirrors only committed bytes; outstanding borrows prevent
  reset or close. These are serialized compressed-input buffers, not concurrent
  PCM pipes or DMA ownership mechanisms.
- Preserve explicit one-borrow-per-line and disjoint-storage contracts, with
  stale/foreign/out-of-order return rejection. Empty input means starvation,
  not EOF. Native status and identity are not CRI ABI compatibility.
- Full host and sanitizer suites pass, including1,555,979 joint assertions per
  lane. Strict SH-4 archive and staged C/C++ consumer links pass. UNI queues,
  live decoder adaptation and audible/hardware qualification remain separate.

## 2026-11-13 — libADX producer-side PCM filters

- Add optional caller-owned filters for decoded PCM and persistent sequence
  filtering. Callbacks run once per channel on nonempty returned spans before
  pipe publication, without feeding filtered samples back into predictor history.
- Preserve filters across stream seeks while excluding discarded samples;
  initialization clears them. Configuration and callback lifetime belong to the
  serialized producer. Callbacks must not block, yield, reenter the source,
  retain PCM storage or change FPU control modes. Rebuild all consumers for
  enlarged decoder and embedding reader/sequence state.
- Fresh full host/sanitizer suites,106 filter cases and794 checker cases per
  lane pass; strict staged SH-4 C/C++ links retain both new setters. No Audio
  interface changed. Audible-time metering, performance and hardware validation
  remain separate from this native PCM hook.

## 2026-11-13 — libADX encoder padding and SFA prefixes

- Admit 0..31 inserted leading frames in supported v3 single-loop headers and
  expose the count in validated metadata. The encoded timeline, predictor
  history and PCM math remain unchanged; nonaligned encoded loop starts remain
  unsupported. Consumers must rebuild for the metadata structure change.
- Add caller-buffer construction of validated 288-byte SFA ADX prefixes, without
  an encoder, allocation, I/O or active-stream mutation. This is not an SFD
  demuxer or live stream-joint insertion implementation.
- Fresh full host/sanitizer suites, including512 initialization cases and794
  checker cases per lane, pass. Strict GCC16.2 SH-4 archive and staged C/C++
  links pass. These are not emulator playback, audible output, physical hardware
  or complete historical format/middleware parity claims.

## 2026-11-13 — experimental bank authoring, validation and upload

- Add an original private ABK authoring tool, byte-safe native validator and
  caller-owned PCM load plan. Validate once at admission, with bounded extents,
  explicit source/output alias rules and unchanged outputs on rejection.
- Add a private SH-4 uploader using existing sound allocation and SPU requests.
  READY requires exact completed bytes and destruction of the final request;
  cancellation and uncertain retirement retain ownership. No bank is published
  to ARM, and no voice is started by this utility.
- Current-base authoring, validator and uploader host tests pass, including
  ASan/UBSan for actual native C. Matching SH-4 builds and both scoped Flycast
  modes pass aligned AUTO-DMA/unaligned AUTO-PIO byte readback and allocation
  restoration. These checks do not establish physical DMA/cache behavior,
  audible playback, final sample-fetch retirement or a stable public bank ABI.

## 2026-11-13 — retained SH4DER frame sampling

- Bind checked RGB565 linear/stride render surfaces once, precompute sparse
  sampling offsets and read only after the identified producer completes.
  Caller ownership covers writes, reads, replacement and shutdown; borrowing
  does not pin storage or retire later GPU readers.
- Add generation/render-tagged metadata history with explicit reset and stale
  slot rejection, plus host regressions and an original bounded RTT fixture.
  No scanout inference, full-frame copy or competing renderer is introduced.
- Host/sanitizer tests and GCC16.2 addon/native links pass. The unchanged native
  RTT fixture passes both Flycast execution modes with explicit RTT readback;
  the default GPU-cache-only path failed CPU pixel checks. Strip-header and
  grade tests, and checked Compact render completion/shutdown, also pass both
  modes. Hardware visibility/performance, pixel-exact blending and remaining
  effect coverage are not established by these emulator results.

## 2026-11-13 — managed audio ports, observation and finite completion

- Add cooperative generation-tagged channel reservations, managed mono playback
  and command receipts. Preserve existing legacy channel allocation and the
  cooperative status getter; capabilities negotiate against the loaded firmware.
- Add bounded ARM observation and finite PCM16 end records, with matching host
  readers and priority refresh. Completion does not free retained sample memory,
  prove final hardware fetch or recover LOST attachments automatically.
- Keep playback linkage optional for non-users, and regenerate the embedded
  firmware after its source build in the same root invocation. Original examples
  exercise reservation, observation, playback and completion.
- Current-base normal and ASan/UBSan model suites, the GCC16.2/ARM7DI full build
  and seven affected consumer links pass. The ARM image matches the object,
  archive member and firmware-using ELFs; repeat builds preserve the firmware
  and embedding without regenerating them. Earlier scoped emulator
  evidence used the separately validated ARM-interpreter build; it is not proof
  for every emulator backend or physical hardware. Full Manatee parity, bank
  publication, audible quality and safe sample reclamation remain open.

## 2026-11-13 — caller-owned SH4DER grading packets

- Build ordered, aligned header/quad packets for SH4DER look grading through
  the existing PVR context compiler. Admit changing inputs/capacity before output
  writes; leave list routing, sorting and resource ownership with the caller.
- The Compact example retains a constant authored grade and submits complete
  packets through one route. No direct-SQ bypass or scanout sampling is added.
- Host/matching sanitizer tests and SH-4 addon/test/example links pass. Host
  context mocks do not establish native packet encoding or visual blending;
  framebuffer exposure, remaining effects and native visual tests remain open.

## 2026-11-13 — reusable bloom and foot-shadow geometry

- Add caller-buffer bloom triangles and SH4DOW foot receiver-plane quads, plus
  a host/load-time ARGB4444 foot alpha-mask bake. Keep projection, clipping,
  materials, texture upload and retirement with the existing KOS renderer.
- Preserve output on insufficient capacity and rejected foot receivers; migrate
  the Compact example to the reusable bloom builder.
- Host and matching sanitizer cases pass; GCC16.2 addon/example links pass.
  Textured foot-shadow rendering, visual/hardware validation and the remaining
  screen-effect integration are not claimed complete.

## 2026-11-13 — reusable Compact SH4DER/SH4DOW composition

- Add caller-owned model-indexed sidecar binding and a reusable Compact callback
  for blade/hologram colors, AO and prepared limb shadows, composed with existing
  lighting through explicit replacement, modulation or saturating addition.
- Preserve actual model indices, UV/offset fields, caller ownership and XMTRX.
  Bind immutable data once; do not revalidate model streams per vertex or frame.
  Migrate the existing Compact example to the addon instead of private callbacks.
- Exact candidate host/sanitizer tests and SH-4 builds/links pass; integration
  host tests and the example link also pass with the six-strip default. Native
  visuals/hardware remain unverified. Full screen-effect integration is still
  in progress, tracked explicitly in the addon's capability coverage document.

## 2026-11-13 — six-strip polygon partition default

- Set the polygon command's internal strip partition length to six (encoded as
  three), inherited by ordinary and two-volume material/Compact compilation.
  Submitted strip vertex counts, sprite and modifier-volume commands are unchanged.
- Correct the legacy `auto_strip_len` field documentation: it enables updating
  the partition setting, not automatic length selection. Preserve its API name.
- Material, Compact binding and cache host suites pass. GCC16.2 KOS rebuild and
  Compact SH4DER example/native packet-regression links pass. The native regression
  has not been run; rendering, retail-hardware behavior and speedup are unverified.

## 2026-11-11 — libADX validated metadata and prefix queries

- Add a read-only validated metadata snapshot and header/endcode prefix probes
  using the existing decoder parser. Preserve outputs on incomplete/error input,
  bound scans to one record and distinguish record validity from file/playback
  completion. adxcheck consumes the snapshot without changing its JSON contract.
- Document deliberate strict native recognition versus SDK permissive recognition
  and supplied-span sizing. Decoder admission, PCM math and Audio are unchanged.
- Add 39 query cases, passing in host and sanitizer suites, and validate staged
  SH-4 C/C++ consumers. Full historical-format and middleware parity remain open.

## 2026-11-11 — KOSdec libADX music foundation

- Add the original, explicitly built libADX addon: incremental mono/stereo ADX
  decoding, coefficient setup, memory/file/direct-disc sources, bounded ACX/AFS
  lookup, loop/sequence controls, seek, PCM pipe and fiber-backed producer.
- Add a fork-native player and filename BGM example with bounded compressed-input
  caching, embedded-loop release and same-format source transitions. Include the
  host adxcheck compatibility tool, synthetic regressions and install targets.
- Preserve explicit SH-4 FMAC arithmetic and pinned SH4ZAM definitions; no codec
  dependency repin, global sound initialization or aggregate-build change.
- Host and sanitizer suites pass; current-fork SH-4 archive/examples and staged
  consumer link pass. Full SDK middleware parity, qualified audible completion,
  reusable sound retirement and retail-hardware validation remain open. libSFD
  and libAHX directories are placeholders, not delivered playback libraries.

## 2026-11-11 — bounded sound-status retry

- Release and restore G2/IRQ state between channel-status snapshot attempts.
  Permit rescheduling only for an ordinary, fully interrupt-enabled thread with
  active preemptive threading; preserve caller exclusion and errno otherwise.
- Keep the eight-attempt limit and EAGAIN result. This is not a wall-time bound,
  writer-progress guarantee, consumed-source clock or hardware-retirement proof.
- Exact-body host regression covers 1,307 checks, including masked/IRQ/inactive
  contexts and failure paths; current-fork SH-4 compilation passes.

## 2026-11-11 — caller-owned SH4DER-PVR foundation

- Pin SH4DER-PVR at 17a1f0623 as an unmodified submodule. Build an optional
  six-source CPU addon with SH4ZAM, retaining both custom dependency licenses.
- Add a Compact blade/hologram callback and caller-owned geometric-bloom example,
  plus host regression for repeated/reversed vertex indices, sidecar rebind,
  preserved vertex fields, output capacity and bloom projection.
- No vendor scene, framebuffer, RTT, fog, allocation or history-buffer owner is
  enabled. Those GPU backends remain separate integration work; no hardware
  performance or visual-parity claim is made.

## 2026-11-11 — upstream VMU size reconciliation

- Merge official KOS through 38aeee7e9, covering the two VMU payload-size fixes.
  Preserve the fork's existing logical-size/capacity/package-offset separation:
  reads, writes, SEEK_END, total, fstat and package writeback already operate on
  logical payload size rather than header-inclusive card allocation.
- No VMU implementation change is needed; retain the fork's exact payload EOF,
  overflow checks, gap filling and metadata handling. Package/chain host tests
  pass; physical VMU validation remains separate.

## 2026-11-05 — upstream reconciliation

- Merge official KOS through d81aa3fda: Maple receive-buffer cache preparation,
  MIE center-first wheel calibration, elf2bin destination/error handling, and
  kos-chain GDB Python/static-build and Windows-host maintenance.
- Preserve the fork's equivalent architecture-before-final-flags environment
  ordering, GCC fallback and SH4ZAM linkage when resolving upstream conflicts.
  No compiler-profile, language-default or dependency-pin change.

## 2026-10-26 — prepared directional lighting

- Add caller-owned basic/extended lighting snapshots that validate and normalize
  directional lights once for reuse across a batch. Preserve existing checked
  APIs, packed-color/error semantics and dynamic point/sample checks.
- Reuse one snapshot per frame in the scene workload, with an opt-in checked
  comparison. No heap allocation, hidden cache, FTRV batching or math-policy
  change. Reprepare when source values change; keep snapshots immutable in use.
- Host parity/sanitizer and scene tests, GCC16.2 builds/links and retained
  Flycast parity/state/complete-scene checks pass. Fixture light admission and
  normalization counts fall from 9,828 to 18. Preparation adds storage/copy cost
  and loses for single-reference host batches; hardware speed is unmeasured.

## 2026-10-26 — SH4ZAM v0.10 compatibility integration

- Advance the official submodule from `9a214a2` to `ad2f890`; build the new
  SH-4/software matrix wrappers and retire obsolete archive members. Refresh
  the exact-base patch, removing the public alignment correction now upstream.
- Document the native matrix/prepared-joint layout change and mandatory clean
  consumer rebuild. Preserve the FFT optimization override and math policy.
- Update the example version check, exercise the out-of-line matrix dispatcher
  on both backends, and restore explicit-patch checker regression coverage.
- GCC16.2 full fork build, C/C++ layout checks, both exclusive fiber-provider
  links, ADX-fiber link, focused host fixtures and selected Flycast checks pass.
  Hardware validation, performance claims and separate numerical investigations
  remain outside this compatibility acceptance.

## 2026-10-21 — checked network software retirement

- Close BBA admission before retiring TX, complete polled RX operations and
  owned DMA/callback tails; prevent late link events from reopening a stopped
  device and serialize worker lifetime, initialization and restart.
- Propagate retirement refusal before protocol/platform destruction or exec
  IRQ masking. Require exact typed checks for custom shutdown-pointer hooks;
  preserve the original refusal reason in a terminal diagnostic wait.
- Retain acquired/partial BBA hardware. Final NIC/SRAM/loader release remains
  held, so BBA-using exit/exec may deliberately wait indefinitely. This is not
  completion of the NetBSD port or physical-hardware validation.
- Host ASan/UBSan and GCC16.2 SH-4 build pass. Five SH archive/link cases,
  five real-library pointer probes and interpreter/dynarec scheduler probes
  pass. Mach-O archive cases explicitly skip; no hardware result is claimed.

## 2026-10-21 — project-driven P1 overlay example

- Convert the original direct-disc example to an explicit slot/member manifest,
  generated linker reservation and trusted package catalog. Remove the bespoke
  two-pass resident link and duplicate identity/ELF checking logic.
- Check the final reservation with the shared project tool; preserve the normal
  heap boundary and document the fixed layout's unused-gap cost. No runtime,
  shared linker, provider, package-format or MMU-default change.
- All 27 host tool tests and both GCC16.2 provider builds pass. Core and SH4ZAM
  fiber variants each pass Flycast interpreter/dynarec with unchanged A/B/A,
  suspended-return, escaped-reference and corruption results. Hardware remains
  unverified; overlay demand paging remains deferred.

## 2026-10-21 — explicit overlay project tooling

- Add a project manifest for game-selected slots and their fixed-linked members.
  Generate P1 linker reservations, member base fragments, trusted resident
  package metadata and per-member size/headroom reports using unchanged DCo@ v1.
- Check final resident P1 NOBITS reservations, section overlap and explicit
  heap/stack/RAM budgets; refuse unsupported resident address layouts.
- Host regression tests and actual GCC16.2 SH-4 member/catalog links pass,
  including deterministic output and invalid final-link bounds. No runtime,
  provider, storage or default build changes. Overlay demand paging is deferred.

## 2026-10-21 — explicit translated overlay consumer

- Add explicit DCo@ profile-2 packaging and `ovl_slot_create_vm`, using the
  existing resident executable VM owner. Fixed-P1 packaging and execution stay
  supported without requiring the MMU; no demand paging or implicit selection.
- Copy and zero through the bounded VM writer, verify/close before publication,
  and retain failed cleanup for retry. Preserve generation tokens, short locks,
  notification ordering and application-owned context lifetime across carriers.
- Add original direct-disc translated A/B/A fixtures, page/staging crossings,
  independent-thread preemption, retained yielding callbacks, safe admission
  negatives and actual-VM fault-injected cleanup retries. Document memory costs.
- Twenty packaging tests, O0/O2 host ASan/UBSan and 204 SHA comparisons pass.
  GCC16.2 builds and both exclusive fiber providers pass Flycast interpreter
  and dynarec. Hardware cache/IIX/timing, real boot/shutdown and whole-system
  memory high-water remain unverified; no Kernel/provider/driver edits.

## 2026-10-21 — resident executable VM foundation

- Add opt-in `kos/vm_exec.h` ownership for fully resident translated code,
  separate from ordinary-data VM APIs. Reserve fixed 4 KiB-aligned slots,
  prepare copied page permissions and write through bounded synchronous copies
  into private backing before publication.
- Preserve the old image on preparation failure. Once replacement begins,
  retain failed-load ownership until cache/TLB retirement and cleanup succeed;
  never release backing merely because publication or cleanup failed.
- Check the caller-owned selected context/ASID at admission boundaries and
  retire physical operand-cache, execution-address instruction-cache and TLB
  state before reuse. No scheduler/provider, storage or default-MMU change.
- Actual-code host/sanitizer and focused page/cache/TLB model tests pass.
  GCC16.2 target builds and original direct-disc A/B/A execution pass with
  core and SH4ZAM fiber providers separately in Flycast interpreter/dynarec.
  Hardware coherence/IIX/timing remain unverified. DCo@ translated loading and
  full cross-carrier/token/callback integration remain a subsequent slice.

## 2026-10-20 — fixed-P1 overlay runtime

- Add independent `DCo@` v1 packaging and restricted static-C SH4 ELF admission,
  preserving the entire original ELF and binding trusted package length/SHA-256.
- Add optional, explicitly built `libkosoverlay`: bounded single-pass direct
  `/cd` loading, exact BSS, cache-before-admission ordering and separate primary/
  close errors. Preparation failure preserves the old image; destructive-stage
  failure leaves the slot empty rather than publishing incomplete code.
- Retain images with stable generation tokens across fiber suspension and
  escaped references. Reject replacement while users remain; keep source,
  teardown and caller-context restrictions explicit. No shared Kernel, Fibers,
  Storage or dependency change, implicit scheduler, or executable-MMU service.
- Add an original two-member example with a real resident BSS slot, two-pass
  linker reservation checks, and direct-disc replacement/corruption tests.
- Eighteen host packaging tests, actual-C O0/O2 ASan/UBSan tests, 204 SHA
  differential checks and GCC16.2 target builds pass. Core and SH4ZAM-backed
  provider executables each pass Flycast interpreter and dynarec. Physical
  cache/timing/boot/shutdown and peak-resource measurements remain unverified.

## 2026-10-20 — overlay build foundation

- Integrate the previously prepared host-only SH ELF load-plan tool. Record
  image identity, execution slot, initialized ranges, exact zero tails and page
  permission intent without treating linker physical addresses as backing.
- Reject malformed, overlapping, out-of-slot and unsupported loading layouts;
  keep metadata deterministic and free of host paths. This is not a runtime
  loader, ABI certification, reservation or executable-MMU service.
- Nine host tests and GCC 16.2.0/Binutils 2.47 compile/link checks at P1 and P0
  addresses pass, including exact BSS, deterministic output and overflow
  rejection. No target execution, cache, DMA or hardware result is claimed.

## 2026-10-20 — sprite transform precision boundary

- Keep quaternion magnitude validation in a private non-inlined helper so
  GCC restores single precision before the sprite transform's SH4ZAM
  FIPR/FSRRA instructions. Preserve validation thresholds, error behavior and
  failure-atomic output; no dependency, public API or compiler-default changes.
- Extend cell regressions for scaled and signed planar rotations, small valid
  magnitudes, zero/near-zero and nonfinite rejection. Relink the target fixture
  when its KOS or SH4ZAM archive changes.
- GCC14 host and Clang16 ASan/UBSan tests pass. GCC16.2 incremental kernel build
  and affected target links pass; cell tests pass Flycast interpreter/dynarec,
  with additional O3, Os and O2 LTO consumer builds passing dynarec. Instruction
  inspection confirms the precision boundary; the broader SH4ZAM integration
  also passes dynarec. Physical hardware and timing remain unverified.

## 2026-10-20 — SH4ZAM quaternion update

- Advance the official SH4ZAM pin from `c18faab` to `9a214a2`, retaining the
  exact existing matrix-alignment patch and unchanged licenses/provenance.
- Add upstream's named XYZ/ZYX/ZXY/YXZ quaternion constructors, including
  16-bit-angle variants, and corrected XYZ Euler extraction. Document the
  deprecated constructor's YXZ-to-XYZ behavior change; no tracked fork caller
  needs migration. No graphics algorithm or fiber-provider change is made.
- Extend the shared host/target release fixture with independent quaternion
  composition, signed/odd u16 angles, Euler round trips, gimbal-lock orientations
  and Z-only extraction. Keep approximate extraction tolerances separate from
  the tighter constructor checks.
- GCC14 host release/cell tests and Clang16 ASan/UBSan release tests pass.
  Fresh GCC16.2 full build, integration/cell/fiber probe links and new C++ API
  compilation pass. Full integration passes Flycast interpreter and dynarec;
  physical hardware and GCC17 experimental-LRA behavior remain unverified.
- Standalone target cell test exposes an existing mixed-precision instruction
  scheduling failure: FIPR/FSRRA execute before single precision is restored.
  Both previous-pin and updated-pin consumers fail the same rotation assertion
  in Flycast. Track separately; do not count that target test as passing.

## 2026-10-20 — upstream reconciliation with GCC retained

- Integrate upstream through `5f834c134`, including the merged VMU timestamp
  and soft-gUSA PRs. Preserve the fork's existing timestamp implementation and
  logical-stack/fiber bounds checking with debug-I/O reporting unchanged.
- Adopt the upstream `env/` layout and optional experimental LLVM/LLD files,
  while retaining GCC as the default and preserving old environment entry
  points. Initialize architecture settings before composing final compiler
  and linker flags; keep SH4ZAM in the link group.
- Preserve GNU17/GNU++17 defaults. Environment-equivalence checks cover old/new
  entry points, implicit/explicit GCC and Dreamcast/NAOMI selection; logical-SP
  host regressions pass in GNU17 and C23. LLVM is not a validated fork toolchain.
- Fresh GCC 16.2.0/ARM GCC 8.5.0 full build and C++ chrono, VM4 direct-disc and
  IRQ-stack target links pass. No new emulator or hardware execution is claimed.

## 2026-10-20 — explicit read-only file residency

- Add file-owned virtual reservations with synchronous page prefetch, bounded
  resident backing, eviction and reload. Publish only fully initialized
  read-only pages; reuse per-file staging and preserve independent pin lifetimes.
- Keep source immutability and final consumer retirement explicit. Cancellation
  is not I/O abort, prefetch is not a lease, and close rejects active loads/pins.
  No automatic page-fault I/O, writeback, compressed paging or overlays are added.
- Add production-code host coverage and an ISO9660 /cd example selecting direct
  GD-ROM. Sanitizers pass 2,615,842 checks; GCC 16.2.0 build/link and recorded
  core/exclusive-addon interpreter/dynarec direct-disc probes pass. Provider
  probes reuse accepted archives with the fresh VM object.
- Physical media/cache/DMA, loader return, hardware timing and ISO mount-drain
  remain unverified. Existing unrelated drivers, providers and defaults are
  unchanged; the example does not use an embedded RAM filesystem.

## 2026-10-18 — resident page pinning and physical byte runs

- Add independent pin tokens for committed ordinary-data regions, with exact
  physical byte-run enumeration. Reject overlapping mapping/backing mutations
  until every covering token is released; preserve unrelated-page progress.
- Keep final ownership explicit: callers retire all handle users and consumers
  before release. Pinning does not submit DMA, synchronize caches, resolve
  faults, select an address space or automatically drain cancelled work.
- Production-code host/sanitizer tests, GCC 16.2.0 build/link and recorded
  core/exclusive-addon interpreter/dynarec pin and carrier-blocking probes pass.
  Provider links reuse accepted provider archives with the new VM object.
  Hardware DMA/cache, loader and emulator-alignment limits remain unverified;
  no VM-buffer driver adapter, file-backed paging or overlay loader is added.

## 2026-10-18 — prepared resident fault recovery

- Add explicit one-page zero-commit and guard tickets with thread-context
  preparation, resident-only fault resolution and separate ticket/page lifetime.
  Preserve fatal handling for unarmed or ineligible accesses; no exception-time
  allocation, I/O, blocking or provider switch is introduced.
- Add production-code host coverage and a full-state translated recovery probe.
  Host checks, GCC 16.2.0 builds/links and recorded interpreter/dynarec recovery
  and complete-provider tests pass. Real-loader return, physical teardown,
  dynarec alignment and target raw-gUSA/nested behavior are not certified.
- This is not DMA pinning, file-backed paging, managed stacks or overlays.

## 2026-10-18 — exception register dumps bypass stdio

- Send generic exception dumps directly to the selected debug device, retaining
  severity filtering, register values, text and stack/unwind calls. A bounded
  private integer converter avoids stdio and shared formatting storage.
- Sanitized host regressions and GCC 16.2.0 emitted-code checks pass. The generic
  fault reproducer now reaches its dump, panic and abort output in the tested
  interpreter; static dump stack use is unchanged. Backend progress, nested
  failures and existing stack formatter limitations remain outside this fix.

## 2026-10-18 — panic output bypasses filesystem locking

- Route panic and abort messages through debug string output so exception
  reporting does not re-enter the mutex-protected filesystem path. Preserve
  abort-message filtering and the existing teardown sequence.
- Actual-function host regressions, GCC 16.2.0 builds and recorded fatal-output
  emulator checks pass. Printed abort output is not proof of completed shutdown
  or loader return; arbitrary debug-backend progress is not guaranteed.

## 2026-10-18 — keep logical-stack recovery visible to the compiler

- Use `__is_defined()` in the saved-stack accessor so disabled soft-gUSA code
  remains compiler-checked. Preserve saved-r1 recovery, marker bounds and the
  existing fiber-aware scheduler checks; do not adopt upstream's deferred check.
- Route the host regression to the real KOS configuration macros. Host checks
  and GCC 16.2.0 consumer compiles pass; helper assembly is identical before and
  after at both O0 and O2 for soft-gUSA and non-atomic modes. No new hardware
  or interrupt-runtime validation is claimed.

## 2026-10-18 — opt-in resident virtual data regions

- Add explicit reserve/commit/protect/decommit/release/query APIs above the
  checked MMU helpers, with zeroed backing and rollback/retirement before reuse.
  Exclude OCRAM; retain caller-owned context/ASID selection and quiescence.
  Ordinary allocation, P1 access, scheduler and memory defaults are unchanged.
- Add focused production-code host regressions and a translated-data probe
  spanning page tables, core fibers and carrier preemption. Current-base host
  and sanitizer checks, GCC 16.2.0 builds/links and both private strict-MMU
  Flycast modes pass. A separate emulator alignment-test failure remains open.
- This is resident CPU data only, not paging, DMA pinning, managed stacks,
  fault recovery or executable loading. No hardware/cache/performance claim.

## 2026-10-17 — upstream empty command-line correction

- Merge upstream `8e704086ecd4990627b85fb4905770dfffe29c00`, including
  `310899d0`: pass a writable, null-terminated argument-pointer array to
  `main()` when `argc` is zero. Preserve fork MMU and shutdown sequencing.
- GCC 16.2.0 kernel rebuild and strict probe links pass. The startup argument
  probe and SH4ZAM integration probe pass in both Flycast modes. No physical
  hardware validation or unrelated startup-policy change is claimed.

## 2026-10-17 — SH4ZAM blend and software-backend update

- Advance the official SH4ZAM pin from `7747e5d` to `c18faab`, preserving
  licenses and the explicit exact-base matrix-alignment correction.
- Add weighted 3x4/4x4 accumulation regression coverage and route API guidance
  to upstream's new documentation site. Existing graphics algorithms remain
  unchanged; the update also fixes software-backend inverse-trig endpoints.
- Clang/GCC host consumers, sanitized release fixtures, fresh GCC 16.2.0
  kernel/addon builds and affected example links pass. The full integration
  probe passes in both Flycast modes. Runtime host endpoint checks reproduce
  the old fast-math NaNs and pass on the new pin. No hardware speed claim.

## 2026-10-14 — LZ4 frame and cancellation regressions

- Add a deterministic 4,693-case resident-frame corpus covering truncation,
  checksums, lengths, dictionaries, multiple blocks and buffer offsets. Each
  case exercises synchronous decode and three stepped output budgets.
- Cover cancellation after partial output, callback-time destruction refusal,
  and job/output reclamation before service continuation. Production decoder,
  service, executor and upstream LZ4 sources remain unchanged.
- All five host lanes pass: Clang GNU17/C2x, GCC GNU17/C23 and Clang ASan/UBSan.
  The GCC 16.2.0 Compact asset consumer builds/links against current core/addon
  archives. Scheduler/IRQ doubles do not prove real executor races; no new
  target execution, DMA/cache, hardware or performance validation is claimed.

## 2026-10-14 — direct-stream cancellation regression

- Extend the no-media fiber/disc probe to cancel queued transfers, active
  transfers and their parent sessions while holding transfer callbacks past
  parent destruction. Verify detached requests cannot be destroyed until their
  callbacks retire. Existing request-engine retirement guards are unchanged.
- GCC 16.2.0 probe build/link and strict warning check pass. Flycast interpreter
  and dynarec runs pass the stream suite (230 checks, six cleanups) and enclosing
  RAM/GAPS routing suite (six sibling steps, fourteen callbacks).
- Transport spies exercise real request/fiber/callback workers, not physical
  disc commands or DMA. Hardware/cache/media and global teardown remain separate.

## 2026-10-12 — exception-return guard and core fiber diagnostics

- Set BL together with RB before restoring saved SPC/SSR on both normal and
  forced exception return. Preserve saved task SR, register layout and public
  ABI; production code has no diagnostic instrumentation.
- Add an emitted-code regression, separate live-SR diagnostic and public core
  fiber-runtime probe. Correct the older local-switch probe's coverage claims.
- GCC 16.2.0 affected builds and target links pass. In Flycast interpreter and
  dynarec modes, corrected live-SR and both fiber probes pass; the original
  return sequence fails the live-BL negative control as expected in both modes.
- No physical-hardware exception-window, NMI/UBC, general nesting or exclusive
  addon-provider validation is claimed. Separate TLS/mutex findings remain open.

## 2026-10-12 — bounded acceptance workflow

- Owner subsequently authorized Integration to commit/publish routine reviewed
  changes to fork master once required gates pass, without another permission
  round. Unresolved designs, upstream PRs and history rewrites remain excluded.
- Keep one owner, patch and compact handoff per issue; reuse matching evidence
  and limit Integration rechecks to the actual diff and acceptance gaps.
- Return specific corrections to the owner and integrate the delivered patch
  rather than rebuilding it from a second investigation. Preserve explicit
  source, runtime and hardware gates.

## 2026-10-12 — upstream TCP close retirement

- Merge upstream `568c3404c292a941bcf550465bd968f4fa249aa2` unchanged: reset
  established connections closed with unread data or receiving payload after
  application close, and bound selected closing states when the peer disappears.
- Fresh isolated GCC 16.2.0 utilities/kernel/addon build and HTTP server example
  link pass. Existing warnings remain. No packet-level runtime, emulator or
  hardware validation; BBA shutdown findings remain separate. SH4ZAM unchanged.

## 2026-10-11 — subsystem-owned review-ready delivery

- Make the assigned subsystem responsible for investigation, implementation and
  focused validation, with an exact diff/evidence handoff for Integration review
  and authorized commit. Return corrections to the owner instead of routinely
  reimplementing findings in Integration.
- Retain the source gate, isolated ownership, shared-interface coordination and
  explicit blockers; replace the blanket findings-only/pre-code-review default.
  Documentation-only workflow change, not a runtime fix or validation claim.
- The subsequent all-helper restart permits a bounded parallel round with
  isolated ownership; review/master integration remains serialized and specific
  research/design holds remain in force.

## 2026-10-11 — SH4ZAM update and local alignment correction

- Advance SH4ZAM from `ae8d4c1` to official `7747e5d`, retaining its source
  history and license. Carry an explicit exact-base matrix-alignment patch;
  setup applies it on request and verification rejects unexpected changes.
- Let native matrices/software state retain their required vector alignment.
  Keep raw-buffer XMTRX paths independent of that type alignment, preserving
  existing SH-4 instruction strings, dispatch checks and precise memory spans.
  The KOS memcpy bridge no longer requires equal native/legacy alignment.
- Isolated validation: eight GCC/Clang host-suite runs and a Clang sanitizer
  run pass; Mac utilities rebuild; GCC 16.2.0 force-rebuilds kernel/addons and
  links six integration/context examples. Focused SH-4 O2 wrapper assembly is
  unchanged apart from source-line annotations. Inherited build warnings remain.
- Follow-up canonical rebuild: utilities, kernel/addons and the six selected
  examples rebuild; all eight host-suite runs pass. SH4ZAM integration (including
  native/raw alignment) and the core fiber context probe pass in Flycast v2.7
  interpreter and dynarec modes. Existing unrelated source edits are preserved;
  this dirty-checkout run does not accept those edits as reviewed changes.
- Rebuild other consumers before use because native containing-layout strides
  can change. No hardware or performance claim; alternate fiber bundles and
  unrelated dirty experiments remain outside this validation.

## 2026-10-10 — upstream IRQ compiler-state correction

- Merge upstream `84ed47b77b2c7823c13e48aea32e4f4c0f3d5b14`: declare the T
  register clobbered by `arch_irq_restore()` so compiler condition-state
  tracking accounts for SR restoration. IRQ masks, saved values and the
  separate exception-return sequence are unchanged. SH4ZAM stays pinned.
- Fresh GCC 16.2.0 SH-4 kernel/addon/utility build and four synchronization/
  context example links pass. Focused C/C++ IRQ-header probes pass at three
  optimization levels; no old-code wrong-branch failure was reproduced.
- GCC 14.4 logical-stack host tests pass. Document the Clang host-header
  limitation; legacy general-threading/C++ examples retain their unrelated
  warning exceptions. No emulator or physical-hardware validation is claimed.

## 2026-10-10 — publication of reviewed integration fixes

- Publish the accepted fiber-event ownership, disc-request retirement and
  ordinary PCC1 single-admission changes as separate focused commits, followed
  by their fork guidance and decision records. Complete fiber bundles and
  upstream PR branches are unchanged.
- Rechecked the exact source against the retained October 3 validation
  artifacts and reviewed the outgoing diffs. The test/build results below are
  retained evidence, not newly executed tests for this publication pass.
- Kernel TLS, mutex and condition-variable findings remain research awaiting
  bounded implementation and regression work. Other unaccepted subsystem
  candidates and private research are not part of this publication.
- No new emulator, physical-hardware, DMA/cache timing or FPS claim is made.

## 2026-10-03 — sequential subsystem implementation policy

- Clarify that Kernel's phased investigation covers its complete primary/shared
  responsibility, starting with coverage and whole-startup mapping, rather than
  treating G2-focused lifecycle work as the entire scope.
- Record D17 and a dependency-ordered implementation plan with one lead lane,
  explicit completion gates and mandatory recorded documentation/SDK/Ghidra
  evidence before dependent changes. Source-only exceptions need review.
- Guidance only; no runtime changes or new validation claims in this iteration.

## 2026-10-03 — request retirement and single-pass cooked-cache loading

- Retain disc requests until completion handling, worker references and callbacks
  retire. Destruction rejects live internal owners; callback-free waits preserve
  wait-then-destroy behavior, while callback-bearing ordinary waits remain early.
  DMA stop policy, GAPS ownership and direct-disc defaults are unchanged.
- Add ordinary PCC1 header-only sizing and single-admission loading APIs, including
  module exports. The chunk-scene consumer now validates the payload once instead
  of three times and avoids redundant native-cache scans. Existing checked APIs
  remain supported; failed admission leaves output storage and draw state intact.
- Fresh isolated validation: 14 modeled request cases pass ASan/UBSan; cache tests
  pass normally and with sanitizers, including 1,888 mutation comparisons; scene
  integration/failure-cleanup checks pass using final public headers. GCC 16.2.0
  rebuilds affected SH-4 code and links three disc consumers plus chunk-scene
  against checkout-local archives. Unchanged build prerequisites are reused.
- These are local integrations, not published branch updates. No new emulator,
  physical-console, timing or FPS result is claimed.

## 2026-10-03 — fiber event ownership and graphics work direction

- Check the caller's fiber-runtime ownership before an already-signaled event
  can return success. Foreign and unattached callers receive `EXDEV`; preserve
  IRQ rejection, owner-main immediate success and child-fiber parking behavior.
- Clarify the public event-wait contract and add the focused ownership cases
  to the existing fiber-sync example. The core provider gains no SH4ZAM or
  Service Executor dependency; alternative provider branches are unchanged.
- Integration reran all 11 modeled host cases with Clang ASan/UBSan successfully
  and rebuilt the affected SH-4 code/example with GCC 16.2.0. Unaffected isolated
  build prerequisites were reused. Reviewed helper interpreter/dynarec logs
  each pass 42 ownership checks and the original synchronization sequence;
  no fresh emulator or physical-console run is claimed by integration.
- Record the accepted host-preparation/repeated-check-removal direction in D16.
  Single-admission cooked-cache loading is assigned work, not delivered code.

## 2026-10-01 — upstream ancestry and MMU example reconciliation

- Merge upstream from `7d0972e32d2d9e7af8d90f52bf0eb5a4dec1bfd7` through
  `d458073c00025f065cc68bebb8ef15ea83264e2b`, preserving the fork's direct-I/O
  defaults and previously adapted ISO9660 locking rather than replacing them
  with upstream's single-lock implementation.
- Retain shared SQ mappings (`SH=1`) through the fork's P2 helper. Add the
  upstream ASID example using startup-owned MMU lifecycle and checked mapping
  failure handling. Keep the existing whole-object keyboard initialization.
- Adopt unsigned Maple enumeration indexes and upstream documentation while
  retaining checked function descriptors and NULL results for invalid masks.
  Update the controller snapshot example's out-of-range index accordingly.
- Import upstream's TCP poll-wakeup lock-order fix and GCC 13.5 profile/patch
  maintenance. The active GCC 16.2 toolchain and pinned SH4ZAM revision
  `ae8d4c190bc52d4e01e8ec7a0a9b5a7b4fd7d69b` are unchanged.
- Host validation: MMU page and TLB models, SQ ownership, all 41 ISO9660 cases,
  and a focused Maple enumeration probe with ASan/UBSan pass. These do not
  establish physical cache, bus or DMA behavior.
- A fresh isolated build of the kernel, addons and utilities passes with SH-4
  GCC 16.2.0 (GNU17 kernel). ASID, controller snapshot, asynchronous ISO9660
  and direct ISO9660 examples compile with GNU17/Wall/Wextra/Werror and link.
  Maple header checks pass for GNU17 and GNU++17. Inherited build warnings
  remain; no emulator or physical-hardware run was performed.

## 2026-10-01 — ISO9660 cache lifetime and storage guidance

- Keep shared sector-cache bytes protected through foreground parsing/copying
  on both backends. Adapt upstream `aeee10d9b2de2d6644be5f303921baa9079717bb`
  without forcing direct async admission under BIOS stream serialization.
- Preserve short descriptor publication/finalizer locking, deferred remount,
  generation-aware snapshot invalidation and callback reentry. Keep I/O errors
  distinct from missing paths and reject failed mount reads correctly.
- Add a production-driver host fixture: 41/41 cases pass on integration rerun.
  Storage also reports 41/41 with ASan/UBSan; its transport, requests and VFS
  scheduling are modeled. Driver and two consumer objects compile with SH-4
  GCC 16.2.0/GNU17/Wall/Wextra/Werror; no full target link or hardware result.
- Correct direct-DMA documentation for segmentation/deadlines, callback
  retirement, caller-owned GAPS leases and existing cooperative stream APIs.

## 2026-10-01 — Kosh and Conio baseline import

- Include editable Kosh and Conio addon sources at upstream revisions
  `535a1f04c141aa61ae176dc2d11c373ab8b6c493` and
  `36c234b93c6ba44b58417c18b45828ec62b4d1c2`, respectively, preserving author
  notices and KOS licensing. Record upstream provenance in each addon README.
- Use in-tree header routing and addon archives; remove upstream Makefile
  writes into kos-ports. Neither library is auto-started or kernel-linked.
- Runtime sources/headers are unchanged; Nindows2 parity remains future work.
- Validation: both archives compile with SH-4 GCC 16.2.0, GNU17, -O2,
  -m4-single. Existing basic, debug-I/O and Kosh examples link to the new
  archives, checked in linker maps. Link smoke uses existing core/SH4ZAM
  artifacts, not a fresh full-kernel build. Inherited keyboard-deprecation and
  directory-entry const warnings remain. No emulator/hardware run.
- No Nindows2 implementation or inherited runtime behavior changes in this import.

## 2026-10-01 — project handoff foundation

- Add `AGENTS.md` for safe fork maintenance and subsystem coordination.
- Add `notes.md` distinguishing accepted decisions from implementation status.
- Start this fork-specific iteration log without altering upstream release notes.
- Adopt the owner's KOS Experimental project name. Relocate the local checkout
  and repair its Git/submodule registration without changing commit history.
- Add 21 selectively loaded component guides under `doc/agents`, with source
  entry points, contracts, interface partners, tests and handoff boundaries.
- Assign AICA/Manatee audio to a dedicated sixth subsystem lane, with distinct
  ARM firmware/toolchain, SH-4 host and DSP validation responsibilities.
- Add dedicated LZ4 and Nindows2/Kosh addon lanes and two guides (23 total).
  Record the owner's Kosh-based parity direction, including substantial Conio
  changes where justified. Initial assignments are source/design audits only.
- Require relevant shared-source/tool consultation for subsystem investigations,
  with pinned binary-analysis evidence and original-reference preservation.
- Inventory 1,390 unique changed/local/side-branch-only paths against pinned
  snapshots. This is a routing audit, not a full correctness certification.
- Validation: documentation structure, source anchors and privacy checks only;
  no runtime changes, target build, emulator run or hardware test in this pass.
- Guidance belongs to the experimental fork, not a blanket upstream submission.
  Private research and host-specific handoffs remain outside the repository.

## 2026-10-01 — documentation publication cleanup

- Master commit: `ebfee67bccd106ca2478f2b6365b7a8462eaed8b`.
- Remove eight unapproved documentation paths from affected published branch
  tips and repair references; corresponding cleanup commits cover eight branches.
- Verified planned documentation-only diffs and scanned all 30 published tips.
- Runtime code and pre-existing local edits were unchanged. Published history
  was not rewritten; local recovery material is kept outside the repository.

## Baseline before maintained iteration records

Selected committed milestones, not a complete retrospective changelog:

| Commit | Recorded change |
| --- | --- |
| `c78d405d4` | Interactive 2D/3D rendering showcases |
| `b86108d1e` | Bounded toon color rounding through SH4ZAM |
| `fa4f144b4` | Cooperative direct-stream waits in the fiber-disc adapter |
| `17996703a` | Direct reads into caller-owned GAPS leases |
| `10b70ede9` | Halt when DMA cannot relinquish its destination |
| `a49ac5762` | SH4ZAM dependency update after v0.9.1 |
| `e861241ac` | Upstream merge through `7d0972e3` |

Milestones above were confirmed in history, not retested during documentation
setup. Uncommitted graphics, G2 and NetBSD-port work is not a released milestone.

## Entry convention

For each accepted iteration: date and ID; subsystem; before/after commits (or
explicitly uncommitted); what changed and why; exact validation evidence and
limits; compatibility/dependency impact; remaining risks and next action.
Keep detailed logs and private paths outside this file. Link reviewable commits
instead of copying every experiment into the published changelog.
