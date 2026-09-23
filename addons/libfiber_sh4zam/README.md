# SH4ZAM fiber runtime and service executor addon

This is an **alternative provider**, not a second simultaneous fiber runtime.
Choose exactly one for an application:

- KOS core fibers: the upstream proposal's KOS-only implementation.
- This addon: its own duplicate fiber/synchronization runtime and context switch,
  plus the Fiber Service Executor, using SH4ZAM directly for XMTRX save/restore.

The two providers intentionally supply the same fiber symbols. Fiber handles,
TLS state, stack resolver ownership, and context objects must never be mixed
between implementations. This is not a runtime-selectable backend or a plugin
which can be loaded into an application already using core fibers.

## Dependencies and build

This initial extraction requires the KOS core-fiber proposal's public headers,
logical-SP fix, continuation-stack resolver and SQ exclusion hook (tested at
`ecde786320cac4a3ee8b50f158eeddfb03fe1fc0`). Those are OS mechanisms, not math.
They are not yet accepted upstream; this addon is not advertised as compatible
with unmodified upstream KOS. It does not access a kernel-private fiber header:
the executor's private cancellation/observer hooks belong to its bundled copy.

Use an independent SH4ZAM 0.9.0 source checkout. No upstream SH4ZAM source is
copied, renamed, or reattributed here. The current register operations inline
from its public headers; clients using out-of-line SH4ZAM routines must also link
their compatible SH4ZAM library. Tested dependency:
`gyrovorbis/sh4zam` commit `0fd3a1e1fa0809d33198c062632b1494ec2f57df`.

With a configured KOS environment:

```sh
make SH4ZAM_ROOT=/path/to/sh4zam all probes
bash check-provider.sh
```

Applications include `kos/fiber.h` and `kos/fiber_sync.h` from the prerequisite
KOS headers, and this addon's `include/kos/fiber_service.h` when using services.
Add this addon's `include` to their include path. Link the **entire** provider
before the normal KOS libraries, as the supplied probe build does:

```sh
kos-cc -o app.elf app.o \
  -Wl,--whole-archive /path/to/libfiber_sh4zam/.build/libfiber_sh4zam.a \
  -Wl,--no-whole-archive
```

Whole-archive selects all fiber components, preventing partial resolution from
KOS's archive. Do not link the kernel fiber object files explicitly or force
whole-archive inclusion of `libkallisti.a`; duplicate-symbol errors are expected
if both implementations are forced in. The link-map audit checks that no kernel
fiber implementation was selected.

## Math and switching contract

`KFIBER_ATTACH_MATH_CONTEXT` uses `shz_mat4x4_t`,
`shz_xmtrx_store_4x4()` and `shz_xmtrx_load_4x4()` directly. There is no KOS
`mat_load`/`mat_store` fallback, scalar-math wrapper or additional assembly
memory clobber. New contexts receive an identity value in backing memory without
clobbering the creator's live XMTRX. Contexts remain 64 bytes, aligned to 32.

General registers, stack, return address and ABI nonvolatile FP registers still
use the copied SH-4 context-switch assembly: SH4ZAM is the accelerator/math
backend, not a CPU scheduler. Allocation, threads, IRQ exclusion, timers, SQ
ownership and semaphores remain KOS OS services.

The default attachment/executor remains lightweight (no fiber-local XMTRX).
Opt in with `fiber_attach_ex(KFIBER_ATTACH_MATH_CONTEXT)` or
`fiber_service_executor_create_ex(KFIBER_ATTACH_MATH_CONTEXT)` when required.
FPSCR/FPUL are not fiber-private; restore normal ABI modes before yielding.
The core's borrowed-stack, owner-thread, synchronization and teardown contracts
also apply to this provider.

## Provenance and maintenance

The duplicate runtime, synchronization and assembly originate from core-fiber
commit `ecde786320cac4a3ee8b50f158eeddfb03fe1fc0`, including the event-ownership
fix and teardown tests. The executor and its probes originate from integrated
fork commit `b554f2b9e64eef3c729b8f63bd07f6d6f0b3291e`. The architecture header and
math backend are adapted here. Copyright notices remain with their authors.

Core fixes must be reviewed and carried to both copies deliberately. They are
not generated from each other during a build. This duplication is intentional,
and neither the core PR nor integrated master is changed by this extraction.
No Sega middleware, BIOS, media assets or reverse-engineering artifacts belong
in this addon.

## Validation gates

The bundled probes cover register/stack switching, sync cancellation, scoped
joined-owner allocation accounting, competing FPU threads, and executor
wake/deadline/IRQ/queue/shutdown behavior. Math probes use SH4ZAM directly,
including FTRV via `shz_xmtrx_transform_vec4()` in the executor probe.

Physical hardware, MMU-on, detached reaping and forced destruction remain open.
These probes do not establish decoder compatibility or a performance gain.

### Extraction results, 2026-09-23

- All eight addon probes passed in Flycast v2.7 (`628bd3dbb`) interpreter and
  dynarec modes: 16 runs, REIOS, 16 MiB RAM, isolated emulator settings.
- Executor math: 32 rounds per service, 64 competing-thread matrix overwrites,
  FTRV checks, and both services retaining state through shutdown cancellation.
- Core math: eight competing-thread handshakes. Teardown: 32 joined-owner cases,
  192 tracked allocations reclaimed, no borrowed-stack frees.
- Eight link maps contain the complete addon provider and no kernel fiber
  implementation objects. The addon archive imports no KOS/libm math.
- Forcing both providers into a relocatable link was rejected with duplicate
  `fiber_attach_ex`/other fiber definitions, as intended.
- All four addon C runtime units passed SH-4 GCC 16.2.0 GNU17 with
  `-Wall -Wextra -Werror` using system-header paths for KOS/SH4ZAM dependencies.

The matching core PR and integrated master were left unchanged. The first local
test-runner invocation expected the wrong service-sync sequence (5 rather than
the probe's actual success value, 6); that harness typo was corrected before the
complete successful 16-run pass. No runtime workaround was needed for it.
