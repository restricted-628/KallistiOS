# libADX release scope

Copyright (C) 2026 Kyle Mayer

The KOS Experimental milestone is published with native implementations or
accepted native equivalents for **142 of 142 applicable ADX entries**. The [function inventory](BASELINE_API.csv) records named
entries and their native coverage. The [completion map](COMPLETION.md) explains
the 12 excluded entries and the limits of each capability.

The native interface uses namespaced `kosdec_adx_*` functions with explicit
owners, source extents, and service collections. Applications need to adapt to
these contracts. Legacy binary interfaces are outside the native
release scope.

## Playback evidence

- Both Flycast BIOS CPU modes qualified the checked profiles, including
  same-title checked restart, supported live transpose and volume changes,
  and the sample, NTSC and PAL compatibility clocks.
- The last two entries, `ADXT_Pause` and `ADXT_DiscardSmpl`, closed at
  `07515908`. A direct v3 stereo loop paused after wrap, discarded the common
  unread prefix, resumed at the first surviving frame, and drained the
  released tail before logical PLAYEND.
- The tested pause profile uses direct unlinked mono or stereo sources and
  the [documented ring and rate settings](README.md#flycast-active-pause).
  Logical PLAYEND follows real source drain and checked stop. The played
  sound allocation remains reserved.

## Remaining qualification

- Retail Dreamcast audio and GD-ROM timing, final device fetch, and safe
  sound-memory reclamation remain unverified. Broader retained playback
  still has two known `EPIPE` cases: group polling with two retained players,
  and the retained stereo live-gain composition. Checked ordinary playback
  has separate evidence.
- Use [FORMATS.md](FORMATS.md) and [COEFFICIENTS.md](COEFFICIENTS.md) for the
  admitted decoder domain, including the unencrypted v4 mono/stereo loop
  extension beyond the baseline fixture set.
- Filename admission checks raw length against KOS `PATH_MAX` before allocation or provider access.
- Stock KOS integration and ADX encoding/conversion are outside this release.
  AFS archive packing is available in [libAFS](../libAFS/README.md); SFD and AHX
  have their own component scopes.
