# PL_MPEG test adaptation

Upstream: https://github.com/phoboslab/pl_mpeg
Commit: c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
Original pl_mpeg.h Git blob: b42f53f89f868a697435ab1e15742f370819ffbc
Original SHA256: 3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9
Original size: 141424 bytes.

The upstream header names Dominic Szablewski and SPDX-License-Identifier: MIT;
the accompanying pinned upstream README identifies MIT. These notices are
preserved. No standalone upstream LICENSE file exists at this revision.
This directory is an explicitly modified source snapshot, not an upstream release.

Local changes for the bounded test consumer:

- plm_video_decode_block stores natural inverse-quantized coefficients: intra
  DC predictor times8 and clipped AC/non-intra levels without the frequency
  premultiplier. Every coded zero/DC/general block calls media_mpeg_transform.
  That hook validates the frozen domain, calls the existing SH4ZAM IDCT once,
  and returns signed residuals. The later float numerical path described below
  retains byte reference storage and placement; the integer IDCT is not selected.
- Fixed-memory buffers do not discard/memmove borrowed immutable input.
- At end of input, a pending reference frame is returned even when the last
  coded picture was B. The original condition lost that delayed reference for
  the generated I/P/B fixture, returning five of its six frames. Clearing
  has_reference_frame still prevents returning it twice.
- Buffer read/skip operations charge the work budget and report truncation
  through the synchronous test context. Out-of-range coefficient scan positions,
  zero motion f_code and incomplete pictures report errors instead of silently
  completing. This is not a full malformed-stream hardening audit.
- Signed full-pixel motion-vector doubling uses multiplication instead of a
  left shift, avoiding undefined behavior for negative values.

The adapter provides a checked fixed allocation arena using PLM_MALLOC/REALLOC/
FREE. It admits dimensions, frame count, default matrices, rate, sequence identity,
input size and end marker before calling the upstream allocator. Adaptations
are checked-in visible edits, not a build-time patch against a moving source.
No proprietary source, tables or assets are used.

Float32 numerical-path amendment (independent implementation):

- DC predictors and natural coefficient block are float32. Syntax levels and DC
  differentials convert once as they enter arithmetic. DC prediction/scaling
  rejects coefficients outside the original natural domain before IDCT.
- Inversequant uses float32 multiply/scaling, direct SH4ZAM floor, and bounded
  float comparisons for saturation (avoiding strict-mode libm min/max calls).
  Oddification is expressed on integral floats, retaining negative floor, zero,
  sign and saturation order. Parser-admitted escape -256 and quantizer0 remain
  in the tested domain; no silent malformed-input policy change.
- Reference interpolation and B averaging use float32 with explicit inner/outer
  floors. Byte reference storage and integer motion addressing remain; no
  fractional samples feed subsequent reference pictures. Reconstruction adds
  rounded float residuals and clips before final byte storage.
- The float-output IDCT dependency is separately reviewed. Its precise output
  rule is legacy binary32 signed-half bias then truncation, not universally
  mathematical nearest/ties-away. The new consumer requires integral float
  residuals and does not roundtrip through int16 for pixel reconstruction.
- Integer bitstream syntax, counts, offsets, lookup indices and final pixel
  representations are intentional boundaries. The unused upstream integer
  IDCT/conversion utilities are not selected as runtime fallbacks.

These arithmetic adaptations are this project's design, not a claim that all
original SDK motion/reconstruction arithmetic was floating point. Original
license/upstream identity remains unchanged. No proprietary routines/tables
were transplanted. Test oracles preserve integer arithmetic independently.

Packaging amendment: the pinned adapted header is unchanged except for trailing-whitespace normalization and that all `plm_*` identifiers are mechanically prefixed `kosdec_sfd_private_plm_*` to avoid collisions with independently linked PL_MPEG. No algorithm change. The decoder and original math helpers receive KOSdec-specific symbol names and installed headers.
