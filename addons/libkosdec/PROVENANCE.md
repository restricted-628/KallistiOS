# Provenance and license

KOSdec's current decoder, memory/stream readers, KOS file/direct-disc adapters, fiber exerciser, ordered PCM queue, index lookup/member views, scalar math and synthetic tests are original KOS
Experimental work, packaged from the previously developed bounded ADX test
implementation. They contain no proprietary SDK decoder source, coefficient
tables, firmware, game media or sample assets.

This original code is covered by the repository's KOS License; see `LICENSE`
in this directory. The decoder's public identifiers and include boundary were
adapted for standalone use, then its header/channel/length admission expanded;
the private float predictor retains the reviewed numerical implementation.
Coefficient admission uses original mathematically generated decision boundaries
for integer rates 1..48000 Hz and cutoffs 0..floor(rate/2). The directed-interval
certificate generator and independent rational oracle are included with the tests.

SH4ZAM is consumed through the fork's pinned dependency, not copied here. Its
separate MIT license and notices remain in `addons/libsh4zam`. This slice uses
its definitions header; the scalar native FMAC primitive is original code.
No existing SH4ZAM vector routine is claimed to implement this ADX predictor.

The PCM pipe is original adapter work using the SPSC ownership/publication
contract of this fork's `kernel/arch/dreamcast/sound/snd_adx_pipe.c`
(Copyright 2026 Joseph Black, KOS License). It binds the independent float readers
and handles arbitrary partial-group ring boundaries; no proprietary SJ code is
included. Source comments retain this attribution.

The host asset checker and its synthetic CLI fixtures are original work. It calls
the unchanged decoder with the explicit host validation math model; it does not
embed or invoke proprietary tools and does not certify target waveform equivalence.
