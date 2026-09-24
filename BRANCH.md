# IIX-aware instruction-cache maintenance

Branch: `pr/icache-iix-safety`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Selects the correct instruction-cache array indices when CCR.IIX is active.

## Included work

- Separate bounded IIX path, preserved normal path and synchronized data writeback semantics.

## Boundaries

- No OCINDEX data-workspace policy, MMU enablement or cache-mode default change.

## Dependencies and intended use

Includes pr/cache-range-safety; parallel to the whole operand-cache topic.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/icache-iix.md](doc/icache-iix.md)
- [examples/dreamcast/basic/icache-iix/README.md](examples/dreamcast/basic/icache-iix/README.md)

Reviewed code snapshot: [`1d8c74de461f`](https://github.com/restricted-628/KallistiOS/commit/1d8c74de461ffbd3fdfb1abae4b062e684b5bfd7).

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
