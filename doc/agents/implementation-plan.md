# Subsystem implementation plan

Owner direction, 2026-10-03: one lead subsystem at a time, with automatic
documentation/SDK/binary research before proposed fixes and implementation.
This is a working sequence, not permission to rewrite all systems or publish.
Integration keeps current assignments and evidence in the private control folder.

## Working sequence

| Order | Lead | First bounded milestone |
| --- | --- | --- |
| 0 | Integration | Preserve and inventory accepted local changes; establish the exact base for each assigned checkout. Publication remains separate. |
| 1 | Kernel/platform | Reconcile shared shutdown and DMA-retirement contracts against manuals, SDK binaries and current consumers; select the smallest checked-result fix after failure policy is agreed. |
| 2 | Storage/direct I/O | Close request/stream, GAPS and VRAM consumer lifetimes against the accepted kernel contract; preserve direct defaults and the existing DMA-failure policy. |
| 3 | Fibers/services | Apply selected storage/adapter improvements to both complete providers, then verify context state, teardown and provider exclusivity. Keep the narrow submission branch separate. |
| 4 | Networking/BBA | Close driver/loader and packet/socket ownership reconciliation, then implement the first bounded NetBSD-backed networking slice. Existing research gates still apply. |
| 5 | AICA/Manatee | Reconcile SH-4 transport and ARM firmware contracts; implement the reviewed drain/lifetime corrections without inventing sample-fetch retirement guarantees. |
| 6 | LZ4 | Integrate justified frame/adapter correctness work and completion/callback contracts; keep codec provenance and format compatibility. |
| 7 | Graphics/SH4ZAM | Review numerical/backend contracts and remaining repeated work; integrate one justified graphics change at a time. Preserve pinned upstream math. |
| 8 | Nindows2/Kosh/Conio | Resolve input/callback/rendering follow-ups, then implement versioned parity slices and complete visual QA. |

Owner clarification, 2026-10-03: Kernel's investigation covers its entire
primary and shared responsibility, in phases, not just G2 or failure handling.
Start with a current coverage inventory including inherited code, then the
whole startup sequence; continue through the remaining Kernel areas. Earlier
narrow lifecycle findings do not establish whole-component coverage. Integration
coordinates phase checkpoints and shared ownership; the owner need not repeat
the broad research scope for each phase. Production changes still require the
evidence review and bounded implementation assignment below.

Maintain each lane's component coverage as examined, implemented, deferred or
blocked; an inventory alone is not a correctness audit. LZ4 precedes further
graphics asset work, and UI follows its input/rendering dependencies. Interface
partners may review a named question without opening a second implementation
workstream. Do not silently switch away from the active lane's agreed program.

## Required source-evidence gate

Before proposing a correction, the lead records a compact private evidence
table for its exact milestone. Integration reviews that table before code:

1. **Question and identity:** affected function/contract, checkout/commit plus
   dirty overlay, dependency pin, SDK/library version and binary hash.
2. **Documentation:** actual manual revision and pages/sections; relevant SDK
   headers, samples and documented API contracts. An index or filename is not
   evidence that the document was read.
3. **Implementation comparison:** exact fork and upstream paths; applicable
   NetBSD/SH4ZAM or other licensed reference behavior. Record differences, not
   an assumption that either upstream or the old SDK must be superior.
4. **Ghidra evidence:** relevant SDK objects/libraries/drivers, verified processor
   and mapping/relocations, functions and callers, instruction-level checks and
   private artifact locations. Use SH-4 versus ARM as the actual binary requires.
   Existing analysis may be reused when hashes/version/mapping/coverage match;
   summaries alone do not pass. No meaningless decompilation of unrelated code.
5. **Conclusion:** documented behavior, observed binary behavior, current fork
   behavior, inference and unresolved conflict separately. Explain how the
   evidence changes or supports the proposed implementation.
6. **Validation and limits:** smallest relevant regression, integration consumers,
   target build/link and applicable execution tests. Hardware-only questions stay
   unresolved without hardware evidence; no invented delay/register semantics.

If no relevant SDK binary exists, the task is purely source-defined, or a tool
or source is unavailable, record the precise reason and substitute evidence.
Integration must review that exception before dependent implementation. Do not
silently skip SDK/Ghidra work or ask the owner to repeat standing permission to
read supplied sources. Access does not authorize installs, hardware writes,
proprietary code copying, publication or unrelated work.

## Per-milestone completion

Evidence review -> bounded implementation in an assigned isolated checkout ->
focused tests -> interface review -> integration/revalidation -> recorded result.
Every implementation proposal names the minimal diff, expected benefit, retained
contracts and why existing checks are sufficient or insufficient. Do not add
defensive scans, wrappers or memory clobbers merely to appear safer.

Integration accepts the milestone, asks for a specific correction, or records
the exact blocking fact/owner decision. A hardware-dependent blocker need not
stop an independent software fix, but the dependency must be demonstrably absent.
Switch lanes only with a recorded disposition and next action. Research is
bounded by the question; it is not an endless whole-SDK audit. New features do
not displace unfinished correctness work without an explicit reprioritization.

Updates to branch bundles, commits/pushes and upstream PRs remain separately
authorized. Keep private paths, research reports and SDK material out of them.
