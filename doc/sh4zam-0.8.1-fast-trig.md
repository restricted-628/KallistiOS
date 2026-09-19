# SH4ZAM 0.8.1 u16 fast-math regression

Reproduced against the unmodified official tag `v0.8.1`, commit
`be71e8a1428374498e8f5e7506c7e244375f4399`, using SH-4 GCC 16.2.0 and Flycast
interpreter and dynarec modes. No physical Dreamcast result is claimed.

## Symptom

`shz_sincosu16()` disagrees with its strict FSCA implementation when the caller
is compiled with `-ffast-math`. The inputs below are cardinal angles in the
unsigned 16-bit full-turn representation.

| Input | Expected sin, cos | Observed fast-math sin, cos |
| --- | --- | --- |
| 0 | 0, 1 | 0, 1 |
| 16384 | 1, 0 | 0.07260841, 0.99736059 |
| 32768 | 0, -1 | 0.14483349, 0.98945600 |
| 49152 | -1, 0 | 0.21610682, 0.97636974 |

The normal strict integration fixture passes the same cardinal-angle checks.

## Source-level cause

In `include/sh4zam/inline/sh4/shz_trig_sh4.inl.h`, the new fast-math path in
`shz_sincosu16_sh4()` calls both compiler trig builtins with
`radians16 / SHZ_F_TAU`. Those builtins take radians. The equivalent full-turn
conversion is `radians16 * (SHZ_F_TAU / 65536.0f)`, not division by tau.
This report identifies the discrepancy; it does not modify upstream source or
claim that a proposed upstream patch has been validated.

## Reproduction in this KOS checkout

```sh
source environ.sh
make -C examples/dreamcast/sh4zam/integration -B fast-trig-probe.elf
```

Run the resulting ELF with serial output enabled. `fast-trig-call.c` contains
only the library call and is compiled with `-ffast-math`. Its separate driver
is compiled with `-fno-fast-math`, checks finiteness and a `3e-4` absolute
tolerance, and prints the actual and expected pairs. The executable reports
`RESULT: FAIL (SH4ZAM fast-math u16 trig)` and returns failure. It is deliberately
not counted as a passing integration test.

## Integration policy

KOS's default flags do not enable fast-math, and the kernel has no call to this
u16 routine. The official 0.8.1 submodule remains unmodified. With this pin,
compile callers of `shz_sincosu16()` without `-ffast-math`/`-Ofast`; this is a
configuration restriction, not an upstream fix. The existing FFT `-O2`
adapter workaround is unrelated and remains necessary for the local toolchain.

No issue or message has been posted upstream by this change.
