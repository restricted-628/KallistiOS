# ADX middleware completion checklist

The objective is full Dreamcast-era ADX middleware parity, with Sega Library
2.00J as the behavioral baseline and an independent implementation. A port's
current asset set helps prioritize work; it does not remove other capabilities
from the objective. [COMPLETION.md](COMPLETION.md) fixes the completion groups;
[BASELINE_API.csv](BASELINE_API.csv) inventories the named shipped interfaces and
maps every declaration to a group. Full per-function behavioral conformance
remains required; an inventory is not proof of implementation.

“Implemented” below means present in the independently written source and covered
by the stated scoped evidence. It does not mean accepted/integrated by itself.
Integration acceptance, host checks, target links, emulator results and hardware
qualification must be recorded separately. Full middleware completion requires
closing every remaining feature group, not only the assets used by one port.

| Area | Current implementation | Remaining work |
| --- | --- | --- |
| Codec framing | Incremental encoding-3, 18-byte/4-bit mono/stereo v3/v5 input; bounded metadata, footer and EOF validation | Audit and implement the other baseline encodings, versions, header extensions and history layouts; distinguish unsupported from malformed input |
| Coefficient setup | Certified general integer rate 1..48000 Hz and cutoff 0..floor(rate/2), exact mathematical Q12 decisions and explicit rejection | SDK transcendental rounding equivalence and any evidenced baseline domain outside these bounds; hardware validation |
| Predictor | Original scalar float/FMAC, group histories, PCM16 conversion and clipping | Complete native arithmetic and hardware conformance, long-run and proprietary behavioral comparisons where evidence is available |
| Resident and streamed input | Fragmented input, waits, backpressure, cancellation and bounded work | Broader middleware source/SJ contracts, transport error recovery and service scheduling |
| File and disc input | KOS file adapter with optional caller-owned compressed read-ahead, exact-PCM host seek/loop/error checks, and asynchronous direct-disc source with explicit retirement | Remaining baseline file/archive services and recovery; physical disc/DMA validation and read-ahead performance measurement |
| Loop and seek | Supported v3 loop records, carried-history repeat/release and unbound decode-from-start seek | Other baseline loop layouts and control semantics; indexed seeking; qualified audible positioning |
| ACX and AFS | Bounded member lookup and source views | Remaining partition/metadata/name/directory services, API coverage, recovery and authoring |
| Sequences | Bounded 16-entry same-format PCM queue with producer controls | Full baseline concatenation/transition behavior, seamless output and multi-stream scheduling |
| Playback | Fiber-fed native player, prefill/poll, same-format source reuse and early source cancellation | Close the native poll producer/consumer contract regression, then qualify output timing, audible pause/end, safe device restart and sound-memory reclamation with Audio |
| Controls | Producer pause/resume/release/cancel with receipts; checked KOS volume/pan and requested output frequency | CRI control units, fades, priority/voice management, complete control/state/error semantics; resolve stereo live frequency-update timing |
| Host tools | Read-only standalone asset compatibility checker | Archive tools, conversion/authoring, encoding and pipeline conformance; no proprietary executable dependency |
| Port integration | Fork-native library and staged headers/archive; independent decoder and service layers | Stock KOS adapters, complete examples/integration guidance and platform validation |
| Completion evidence | Named baseline declaration inventory; focused host, sanitizer, SH-4 build/link and scoped emulator evidence | Per-function behavioral conformance, real hardware and performance validation |

The coefficient domain is integer rates 1..48000 Hz and cutoffs 0..floor(rate/2).
Rates above 44.1 kHz are an original extension beyond the baseline ADX Talk header's
maximum. Native player output remains 8000..48000 Hz. See
[COEFFICIENTS.md](COEFFICIENTS.md) for the numerical contract and certification.

Codec acceptance, decoded/submitted PCM counts, command acknowledgement and
software joins do not establish audible completion or physical sample retirement.
Each delivery must state its evidence level and unresolved gates. Hardware and
performance results must not be inferred from a host or emulator pass.

SFD and AHX remain separate components under the KOSdec umbrella. They are deferred
while ADX is completed; this checklist does not describe their codec requirements.

## Completion order and acceptance ownership

1. **Audio leads poll-error closure; KOSdec reviews the consumer.** Identify the
   actual failing operation and partial-progress contract before changing retry
   behavior. Preserve first errors, callback accounting and retained resources.
   Acceptance requires a focused producer regression and the matching player test;
   the name EAGAIN alone is not permission to retry or discard an error.
2. **Audio supplies a valid sequential/paired output comparison.** Match firmware,
   controls and complete reference windows. KOSdec reviews the captured results.
   Queue admission, console completion and waveform acceptance are distinct.
3. **Integration selects any production control interface.** KOSdec then validates
   original end-to-end ADX cancellation/seek/rate transitions and steady refill
   through that accepted interface. Private experimental entry points are not
   libADX dependencies; unsupported firmware needs an explicit policy.
4. **Audio owns clock and device retirement; KOSdec owns consumer lifetime.**
   Qualify stop/restart, source/output position and retained-resource behavior
   with injected errors and cancellation. Software join alone cannot free sound RAM.
5. **KOSdec completes remaining codec, middleware, archive and tooling groups.**
   Map each baseline API/behavior obligation to an implementation and conformance
   test. Coordinate Audio for control units/voices, Storage for transport and
   Fibers for scheduling. The missing function-level map remains an explicit gap.
6. **KOSdec supplies stock-KOS adapters and integration coverage.** Preserve the
   fork implementation's contracts and test both builds. Physical timing,
   performance and hardware-specific behavior remain separately qualified gates.

Each later feature group needs a bounded implementation and named acceptance test.
This ordering prioritizes the dependent port without removing full ADX middleware
parity or treating Manatee's broader scope as already delivered.
