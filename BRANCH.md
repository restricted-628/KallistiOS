# Exclusive TMU1 ownership

Branch: `pr/tmu-channel-ownership`

Role: Narrow upstream candidate

Description snapshot: 2026-09-24

Adds typed ownership/configuration for optional TMU1 use while preserving TMU0 scheduling and TMU2 uptime.

## Included work

- Claim/configure/start/stop/snapshot/release, checked time conversion and pending-event preservation.

## Boundaries

- No scheduler/uptime algorithm changes, POSIX clocks, RTC policy or software timer worker.

## Dependencies and intended use

Independent hardware timer topic.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/tmu-channel-ownership.md](doc/tmu-channel-ownership.md)
- [examples/dreamcast/basic/threading/tmu1-channel/README.md](examples/dreamcast/basic/threading/tmu1-channel/README.md)

Reviewed code snapshot: [`21e6efd625bd`](https://github.com/restricted-628/KallistiOS/commit/21e6efd625bd46724694a8d1175489d511f64b8f).

Common ancestor with the inspected upstream master:
[`55da82831f4c`](https://github.com/restricted-628/KallistiOS/commit/55da82831f4cadef233b02c95493011428fb917f).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
