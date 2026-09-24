# Core fibers for upstream review

Branch: `pr/core-fibers-submission`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Narrow core-fiber runtime proposal, distinct from the complete pr/core-fibers SDK bundle.

## Included work

- Continuation runtime, cooperative synchronization, optional XMTRX preservation, scheduler/unwinder stack recognition and SQ exclusion.
- Public exports and runtime/context/math/teardown probes.

## Boundaries

- No SH4ZAM dependency, Service Executor, codecs, renderer, timer-service policy or MMU-on default.

## Dependencies and intended use

Includes pr/sh4-logical-stack as a prerequisite. Use this topic for focused fiber review, not the complete integration bundle.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/core-fibers.md](doc/core-fibers.md)
- [utils/irq-stack-test/README.md](utils/irq-stack-test/README.md)

Reviewed code snapshot: [`ecde786320ca`](https://github.com/restricted-628/KallistiOS/commit/ecde786320cac4a3ee8b50f158eeddfb03fe1fc0).

Common ancestor with the inspected upstream master:
[`804b3195ebd1`](https://github.com/restricted-628/KallistiOS/commit/804b3195ebd1a06a27cc2b3a5eacf7a2429040a3).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
