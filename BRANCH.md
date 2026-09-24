# Stereo PCM16 store-queue uploads

Branch: `pr/stereo-sq-mapping`

Role: Stacked upstream candidate

Description snapshot: 2026-09-24

Corrects per-channel SQ mappings, PCM sample order and partial tails in stereo uploads.

## Included work

- Independently located AICA destinations, bounded G2-locked batches and no heap staging.

## Boundaries

- No codec, playback engine redesign, automatic stereo publication atomicity or measured audio throughput.

## Dependencies and intended use

Includes pr/store-queue-safety. Keep mapping ownership and the upload consumer in dependency order.

This is a candidate for focused upstream review, not an assertion of acceptance.
Keep the code topic separate from unrelated integrated-fork features. The
fork-navigation documentation can be omitted from a final upstream code series.

## Source and detailed contracts

- [doc/stereo-sq-upload.md](doc/stereo-sq-upload.md)
- [examples/dreamcast/sound/stereo-sq/README.md](examples/dreamcast/sound/stereo-sq/README.md)

Reviewed code snapshot: [`08de2b701c8b`](https://github.com/restricted-628/KallistiOS/commit/08de2b701c8b40ebadae996c492edaa4844004aa).

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
