# SH4ZAM fiber runtime and service executor addon

This is the **full alternative fiber stack**, not an executor layered on core
fibers. The complete `addon/fiber-service-sh4zam` branch selects it exclusively;
start with [the bundle guide](../../FIBER-BUNDLE.md). The two bundles provide:

- KOS core fibers: the upstream proposal's KOS-only implementation.
- This addon: its own duplicate fiber/synchronization runtime and context switch,
  plus the Fiber Service Executor, using SH4ZAM directly for XMTRX save/restore.

The two providers intentionally supply the same fiber symbols. Fiber handles,
TLS state, stack resolver ownership, and context objects must never be mixed
between implementations. This is not a runtime-selectable backend or a plugin
which can be loaded into an application already using core fibers.

## Dependencies and build

This complete checkout contains the KOS public fiber headers, logical-SP fix,
continuation-stack resolver and SQ exclusion hook. Those are OS mechanisms,
not a second fiber runtime and not math.
They are not yet accepted upstream; this addon is not advertised as compatible
with unmodified upstream KOS. It does not access a kernel-private fiber header:
the executor's private cancellation/observer hooks belong to its bundled copy.

SH4ZAM defaults to this checkout's pinned `addons/libsh4zam/upstream` submodule.
No separate source checkout is required. No upstream SH4ZAM source is
copied, renamed, or reattributed here. The current register operations inline
from its public headers; clients using out-of-line SH4ZAM routines must also link
their compatible SH4ZAM library. Tested dependency:
`gyrovorbis/sh4zam` commit `0fd3a1e1fa0809d33198c062632b1494ec2f57df`.

With a configured KOS environment:

```sh
make all probes
bash check-provider.sh
```

Applications include `kos/fiber.h`, `kos/fiber_sync.h`, and explicitly
`kos/fiber_service.h` when using the Service Executor. The root build installs
`libfiber_sh4zam.a`; this branch's standard KOS link group selects it
automatically. Normal applications need no custom provider link flags.

The kernel archive contains **no core fiber implementation**. Runtime,
synchronization, context switching, XMTRX preservation and the executor all
come from this addon. There is no fallback or runtime selector. Do not bring
core fiber objects or archives from another checkout into this build.

The standalone addon probes force the entire addon archive for auditing;
ordinary applications can pull only the needed members because no competing
provider exists in the kernel. The shared disc adapter uses the selected public
fiber API and does not add a runtime or require the Service Executor.

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
and the narrow upstream proposal is preserved on `pr/core-fibers-submission`.
The complete addon branch deliberately excludes the competing kernel provider.
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
