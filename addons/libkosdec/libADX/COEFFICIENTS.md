# Coefficient setup

The codec accepts integer sample rates 1..48000 Hz and integer high-pass cutoffs
0..floor(rate/2). Out-of-domain or null-output requests fail without writes.
This is an explicit implementation domain, not a claim that every reference
API accepts every combination. Rates above 44100 Hz are an original extension.
Native player output remains 8000..48000 Hz; low-rate sources need an explicit
in-range output override, which changes pitch and duration.

## Dreamcast rounding

Versions 3 and 5 through 44100 Hz use the same rounded coefficient setup.
Header setup rounds the binary32 product of the cutoff and 2π before dividing
by the rate. The integer significand calculation reconstructs that angle with
ties to even. The Q12 lookup represents changes in the modeled setup output
at consecutive angle words. It includes local reversals caused by intermediate
rounding. Blocks of up to 32 transitions have an absolute checkpoint followed
by variable-length angle deltas and two signed coefficient deltas per entry.
Use `tests/pack_rounded_setup.py` to pack ordered numerical transition values.
The table contains output pairs. It uses no reference polynomial or runtime code.

At 44100 Hz and a 1 Hz cutoff this setup returns {8192,-4096}. It matches the
established profiles at 500 Hz. The argument calculation leaves FPSCR
untouched. Target lookup uses at most 31 transitions after checkpoint search,
at header setup only. The rounded table adds about 60 KiB of read-only data.
Tests run the header and PCM path for both versions at the low-cutoff
rounding boundary and check that higher-rate extensions use the setup below.

## Exact-real extension setup

For r = cutoff/rate, let b = sqrt(2)-1, a = sqrt(2)-cos(2*pi*r), and
p = b/(a+sqrt(a*a-b*b)). The coefficient contract is
`q0 = trunc(8192*p)` and `q1 = -trunc(4096*p*p)` using the mathematical real
values. Zero cutoff gives {8192,-4096}; Nyquist gives {708,-30}. This does not
model the reference implementation's intermediate rounding.

As r increases, both positive coefficient magnitudes decrease monotonically.
The runtime searches two original tables of transition thresholds using exact
64-bit cross-products. Each table entry is a Q32 ratio, with the transition
comparison `cutoff*2^32 >= threshold*rate`. The tables contain 7483 and 4065
entries (46192 read-only bytes). Setup performs at most 13 comparisons per table,
without allocation, callbacks, yielding, target transcendental functions or FPU
mode changes. This cost is paid during header setup, not per decoded sample.
The sample predictor remains the original single-precision float/FMAC kernel;
integer coefficients describe the codec's quantized metadata.

## Certificate construction

`tests/generate_setup.py` inverts the pole expression at every integer coefficient
transition. It bounds the resulting cosine using outward-rounded 96-bit fixed-point
intervals. Pi comes from rational Machin alternating-series bounds, square roots
from integer square-root bounds, and cosine from 32 Taylor terms with an explicit
next-term remainder. Host libm only proposes a Q32 cell; interval comparisons
must prove that the exact transition is strictly inside the cell.

Distinct reduced fractions with denominators at most 48000 differ by at least
1/48000^2, which exceeds one Q32 cell. Thus a cell contains at most one admitted
cutoff/rate ratio. The nearest bounded-denominator fraction to the cell midpoint
finds that ratio if one exists. Interval comparison determines which side of the
true transition it occupies. The stored threshold is the cell's upper endpoint,
or its lower endpoint when the unique ratio has already crossed the transition.
For the latter case, the generator verifies that the lower endpoint itself is
not an admitted fraction. This preserves exact decisions throughout the domain.
Every interval decision is asserted; insufficient precision aborts generation.

The generator certifies 11548 transitions and identifies 1926 near-boundary
fractions. Regenerate with `python3 tests/generate_setup.py --proof PATH` to retain
the detailed certificate. `tests/generate_coefficients.py` independently bounds
the forward equation using exact rational arithmetic; the expanded PCM generator
uses that oracle rather than the runtime table.

## Validation boundaries

The setup tests cover transition neighbors, the near-boundary fractions, scaling
invariance, zero/Nyquist endpoints and invalid arguments. Expanded tests cover
nonstandard rate/cutoff pairs as well as the original eight 500 Hz profiles,
using independent binary32 PCM vectors, stream fragmentation, loop/release and
seek cases. Host checks, SH-4 execution and real hardware are distinct evidence
levels. No playback performance or physical sound retirement follows from a
coefficient or PCM test pass.

The rounded map was checked against the binary32 model's transitions and their
neighbors. The integer argument calculation matched strict IEEE binary32
arithmetic at all 486,202,500 nonzero rate/cutoff pairs through 44100 Hz. A
SuperH p-code execution of the linked baseline routine matched 185 model witnesses,
including all 140 differences found in the original eight-rate sweep. A separate
run of that linked routine matched the native pair at all 22,051 integer
cutoffs from zero through Nyquist at 44.1 kHz. Retail execution of the
reference routine remains unverified.


Private same-guest comparisons against the reference mono/stereo routines
matched 8,064 PCM values and 504 stored histories in both Flycast CPU modes.
The test covered the 13-bit scale, both ignored scale-word bits, seeded history,
clipping and group carry. Reference decoding of multiple groups in one call preserves
float history between them and differs from one-group calls on the test data.
The native decoder fixes that boundary at each group for input-fragmentation
invariance. Retail FPSCR and reference scheduling equivalence remain unverified.

A whole-record comparison on the 11 standalone baseline ADX samples matched
51,457,454 visible PCM values per Flycast CPU mode at the same one-group
boundaries. Each run checked carried histories and validated the native
footer for every record. For a partial final group, the native history
reflects the last admitted samples. The stock primitive also consumes its
padding when called for a whole group. The comparison covers linear decoding
with the matched schedule. Original player scheduling and physical playback
have separate evidence.
