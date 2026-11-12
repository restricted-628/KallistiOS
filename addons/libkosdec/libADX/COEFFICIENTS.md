# Coefficient setup

The codec accepts integer sample rates 1..48000 Hz and integer high-pass cutoffs
0..floor(rate/2). Out-of-domain or null-output requests fail without writes.
This is an explicit implementation domain, not a claim that every baseline SDK
API accepts every combination. Rates above 44100 Hz are an original extension.
Native player output remains 8000..48000 Hz; low-rate sources need an explicit
in-range output override, which changes pitch and duration.

For r = cutoff/rate, let b = sqrt(2)-1, a = sqrt(2)-cos(2*pi*r), and
p = b/(a+sqrt(a*a-b*b)). The coefficient contract is
`q0 = trunc(8192*p)` and `q1 = -trunc(4096*p*p)` using the mathematical real
values. Zero cutoff gives {8192,-4096}; Nyquist gives {708,-30}. This does not
claim bitwise equivalence to a proprietary SDK's transcendental implementation.

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
