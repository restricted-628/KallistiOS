# Subsystem implementation plan

Owner direction, 2026-10-03: one lead subsystem at a time, with automatic
documentation/SDK/binary research before proposed fixes and implementation.
This is a working sequence, not permission to rewrite all systems or publish.
Integration keeps current assignments and evidence in the private control folder.

Owner amendment, 2026-10-11: the assigned subsystem delivers the implementation
and validation, not just findings for Integration to implement again. This
replaces the blanket report-only/pre-code-review default, not explicit audit-only
assignments, specific holds, parked lanes or the source-evidence requirements.

## Working sequence

Owner exception, 2026-10-11: "get them all working again" authorizes a parallel
round of bounded helper assignments, each with an isolated checkout and one
deliverable. The private assignment board names the scopes and interfaces.
The queue below remains the dependency/integration order, not a parking order
for this round. Specific evidence/design holds remain; acceptance and master
integration stay serialized. This does not authorize every lane's whole backlog.

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
source gate and bounded implementation assignment below.

Maintain each lane's component coverage as examined, implemented, deferred or
blocked; an inventory alone is not a correctness audit. LZ4 precedes further
graphics asset work, and UI follows its input/rendering dependencies. Interface
partners may review a named question without opening a second implementation
workstream. Do not silently switch away from the active lane's agreed program.

## Required source-evidence gate

Before proposing a correction, the lead records and checks a compact private
evidence table for its exact milestone. Within a bounded implementation
assignment and established contracts, proceed to code after completing this
gate; Integration reviews the evidence with the delivered change. Exceptions
requiring an earlier review are listed below:

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

The assigned owner investigates -> completes the source gate and required
interface coordination -> implements in its assigned isolated checkout -> runs
focused validation -> delivers a review-ready change. Integration reviews the
exact diff/evidence, performs risk-appropriate reproduction/integration checks,
and integrates/commits when authorized. Reuse verified evidence and existing
patches; do not routinely repeat the investigation or implement the same fix
twice. Return requested corrections to the owning agent.

An assignment names the owner, issue, checkout/base, allowed scope, retained
contracts and completion criteria. It need not prescribe the fix before the
investigation. Every fix explains the minimal diff and expected benefit. Do not
add defensive scans, wrappers or memory clobbers merely to appear safer.

Stop only the dependent work and request a precise disposition when an explicit
audit-only assignment or hold applies, a design/policy choice is missing, the
fix needs an unassigned ABI/default/behavior change or scope expansion, evidence
is missing, shared-interface coordination is unresolved, or no isolated checkout
is assigned/overlapping work prevents safe edits. Source-only exceptions still
require Integration review before implementation; an assignment may include that
review. Do not fabricate hardware conclusions to pass a gate. If investigation
finds no justified change, deliver an evidence-backed no-change result.

### Acceptance loop — avoid duplicate work

Owner clarification, 2026-10-12: investigation must lead to an accepted change,
not repeated audits and an independently reconstructed patch.

1. At assignment, name one issue, owner, exact base, allowed files and the
   specific checks needed for acceptance. Include known prerequisite patches
   and interface reviewers; do not discover packaging requirements at the end.
2. Carry one owned patch and compact handoff through investigation, code,
   validation and review. Link existing evidence instead of writing parallel
   reports. A justified no-change result also closes the assigned question.
3. Integration checks the actual diff, scope, evidence identity and relevant
   interfaces. Reuse matching source/Ghidra/test artifacts; rerun only checks
   needed for risk, reproduction or the changed integration base. Do not repeat
   the whole investigation merely because a different chat is reviewing it.
4. End review with acceptance, a specific correction for the same owner, or an
   exact blocker and the evidence/decision needed to clear it. A missing check
   is a validation task, not a new research assignment. Do not reopen accepted
   findings without a named contradiction, missing artifact or affected change.
5. Integrate the delivered patch intact when authorized. Resolve mechanical
   conflicts in Integration; return semantic changes to the owner. On a base
   update, inspect overlap and rerun affected checks, not every lane's research.
   Record the accepted patch identity and resulting commit; publication follows
   notes.md D18. Do not collect unrelated fixes into a newly engineered mega-commit.

Maintain one current private queue of delivered changes with disposition and
one next action each. Close ready work before expanding that lane's backlog.
This narrows duplicate effort; it does not waive source gates, required runtime
checks, unresolved hardware claims, explicit holds or commit/push authority.

### Review-ready handoff

Keep one compact private handoff with links to existing artifacts:

- Exact checkout, branch, base SHA, dependency pins and identified dirty overlay;
  owned file list and reproducible diff/patch including new files. Commit IDs
  may substitute only when local commits were separately authorized.
- Root cause, why this fix, retained contracts, relevant source-evidence links
  and completed interface reviews; separate facts from remaining hypotheses.
- Exact validation commands, compiler/options and results, with before/after
  reproduction where feasible. Identify unrun checks and distinguish host,
  target-link, emulator and hardware evidence.
- Remaining risks/dependencies, blockers and one concrete next action; a concise
  proposed commit message explaining why. Exclude unrelated edits and private
  research from the proposed public diff.

Review-ready means the owner has implemented and checked the bounded change,
not that Integration has accepted it or that all hardware gates passed. Do not
create bulk public reports or fixtures merely to package a handoff.

Integration accepts the milestone, asks for a specific correction, or records
the exact blocking fact/owner decision. A hardware-dependent blocker need not
stop an independent software fix, but the dependency must be demonstrably absent.
Switch lanes only with a recorded disposition and next action. Research is
bounded by the question; it is not an endless whole-SDK audit. New features do
not displace unfinished correctness work without an explicit reprioritization.

Under notes.md D18, Integration may commit/publish routine reviewed changes to
fork master after required gates pass, without another permission round. Helper
publication, other branch bundles and upstream PRs remain separately authorized.
Keep private paths, research reports and SDK material out of all public diffs.
