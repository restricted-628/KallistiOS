# Application-fiber direct-disc adapter

Branch: `addon/fiber-disc`

Role: Integrated addon topic

Description snapshot: 2026-09-24

Optional libfiber_disc adapter over the public fiber/event and asynchronous direct-disc request APIs, carried on an integrated fork snapshot.

## Included work

- Cooperative child-fiber waiting with an application-owned pump/dispatch loop.
- Queue/callback retirement before buffer reuse; RAM and direct-to-VRAM examples.

## Boundaries

- No new worker thread, hardware driver, math backend or Fiber Service Executor dependency. GAPS lease waits and stream-session waits are not wrapped.

## Dependencies and intended use

Requires exactly one compatible fiber runtime plus the direct-request infrastructure. Both complete bundles include the adapter; prefer them or master for current integrated fixes.

This branch includes integrated fork history. Do not submit its complete diff
as a single upstream kernel change.

## Source and detailed contracts

- [addons/libfiber_disc/README.md](addons/libfiber_disc/README.md)
- [examples/dreamcast/cdrom/fiber-vram/README.md](examples/dreamcast/cdrom/fiber-vram/README.md)

Reviewed code snapshot: [`2ea85dbd13b1`](https://github.com/restricted-628/KallistiOS/commit/2ea85dbd13b10d277920a23a7f724070f737005a).

Common ancestor with the inspected upstream master:
[`fcfa7d869471`](https://github.com/restricted-628/KallistiOS/commit/fcfa7d869471591ca1c777543261a7bfea7cb726).
This records the inspected baseline, not a claim of being rebased to today's upstream.

The description pass changes documentation only. It does not rerun code tests,
prove hardware behavior, or certify every inherited feature. Follow the linked
contracts and reproduce the relevant host, target and emulator checks; physical
hardware validation remains a separate gate. Private research and uncommitted
experiments are not part of this description.

[All published branches](https://github.com/restricted-628/KallistiOS/blob/master/BRANCHES.md) ·
[Integrated fork differences](https://github.com/restricted-628/KallistiOS/blob/master/FORK.md) ·
[Official KallistiOS](https://github.com/KallistiOS/KallistiOS)
