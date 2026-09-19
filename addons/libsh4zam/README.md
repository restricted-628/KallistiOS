# SH4ZAM KOS build adapter

SH4ZAM is developed by Falco Girgis and the SH4ZAM contributors at
https://github.com/gyrovorbis/sh4zam. Its upstream MIT license and author
notices are preserved unchanged. This directory only supplies the KOS build
adapter and integration documentation; the library is not a KOS-authored fork.

## Source ownership and initialization

`upstream/` is a Git submodule pinned to
`0bacf4b336368c0b47864ce9eeb59e7c07904b51` (0.8.0). It contains unmodified
upstream source and history. No local source or documentation patches are
applied. Updating this dependency is a separate reviewed change, not an
automatic checkout of its latest branch.

Clone KOS with `--recurse-submodules`. For existing checkouts, or after changing
KOS revisions, run from the KOS root:

```sh
git submodule update --init --recursive
python3 utils/check-sh4zam-source.py
```

The checker verifies the Git index's pinned revision, clean upstream checkout,
official repository URL, public header symlink, and unchanged license copy.
It replaces the former copied-source hash manifest and maintenance patch.
Ordinary GitHub source ZIPs and `git archive` do not embed submodule contents;
use a recursive clone, or explicitly include the pinned dependency when making
a complete source distribution. A missing checkout produces an actionable
addon build error rather than downloading code during compilation.

## KOS build and linkage

The library remains a KOS addon. `addons/include/sh4zam` is a relative symlink
to the submodule's headers, so existing `#include <sh4zam/...>` users do not
change. The KOS recipe builds the upstream sources into `.build/` outside the
submodule and produces `addons/lib/dreamcast/libsh4zam.a`.

The target build uses the optimized SH-4 backend and implicit TLS. The upstream
portable software backend remains available for host applications. KOS's
current default link group includes `-lsh4zam`; this migration does not make
the graphics stack optional or move it into a separate port.

KOS header/library search paths precede kos-ports. Preserve that ordering to
avoid mixing this pin with another installed SH4ZAM revision. KOS Doxygen
excludes symbolic links; SH4ZAM's own API documentation remains upstream rather
than being patched to fit KOS documentation groups.

## FFT compiler constraint

The former broad `memory`-clobber patch is removed. Upstream's explicit
read/write memory operands are used without alteration.

With the local SH-4 GCC 16.2.0 toolchain, the unchanged eight-point FFT fails
register allocation at `-O0` (`GENERAL_REGS`, impossible asm constraints).
The same source compiles at `-O1`, `-O2`, `-O3`, `-Os`, and `-Og`, using the
KOS GNU17, `-m4-single` environment. This establishes a configuration-specific
build limitation; it does not establish which configuration caused the
historical failure or quantify a performance difference.

The adapter defaults `SHZ_FFT_CFLAGS=-O2` for that translation unit only, after
the caller's general flags. This also permits an otherwise `-O0` debug build
without rewriting upstream assembly constraints. Debug information is retained;
optimized stepping may differ. To choose the tested debug-oriented alternative:

```sh
make -C addons/libsh4zam SHZ_FFT_CFLAGS=-Og
```

Reproduction, after sourcing `environ.sh` (substitute any tested optimization):

```sh
kos-cc -O0 -std=gnu17 -Wextra -Werror -DSHZ_TLS_MODEL=SHZ_TLS_IMPLICIT \
  -c addons/libsh4zam/upstream/source/sh4/shz_complex_sh4.c \
  -o /tmp/sh4zam-fft-probe.o
```

## Floating-point context

SH4ZAM routines using XMTRX clobber the floating-point register back bank.
Retaining that matrix across a cooperative yield requires the fiber runtime's
`KFIBER_ATTACH_MATH_CONTEXT` option, which saves/restores a 64-byte XMTRX.
It does not supply a separate FPSCR, FPUL, or TLS environment per fiber.
Restore temporary FPU mode and exception-enable changes before yielding.
KOS threads preserve floating-point state through their scheduler.

Build, host, and emulator checks are not physical-hardware performance or
numerical certification. The broader KOS-consumer math audit remains separate
from this source-dependency migration.
