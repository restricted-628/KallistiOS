# Core cooperative fibers — upstream proposal

This topic proposes the core fiber runtime as a KOS facility. It deliberately
excludes the Fiber Service Executor, worker/dispatcher policy, deadline queues,
mailboxes, codecs, graphics, SH4ZAM dependencies, timer-event services, and any
change to default MMU/cache policy. It is stacked on `pr/sh4-logical-stack`.
Maintainer acceptance and runtime validation of this isolated branch remain
separate from the existence of the implementation in the integrated fork.

## Core contract

- A fiber is a cooperative continuation within one owning KOS thread, not an
  independently scheduled kernel thread.
- Stacks are caller-owned, aligned, mapped, writable, and kept alive until
  destruction. Creation rejects overlap with sibling and main stacks.
- Fibers share the owner's priority, wait identity, MMU context, GBR/compiler
  TLS, KOS TLS, newlib state, errno, and working directory. Blocking a normal
  KOS call blocks the entire owner thread, not only its current fiber.
- Explicit transfers save the SH-4 nonvolatile C ABI registers, stack, return
  continuation, and interrupt state. Switching from an IRQ or with interrupts
  masked is forbidden. Kernel preemption remains a separate full-thread
  context switch.
- Fibers may not transfer while their owner holds a store-queue transaction.
  Ordinary mutexes also remain thread-owned; callers must not yield with one
  held. This proposal does not add counters to ordinary mutex or SQ operations.
- `fiber_sync.h` supplies fiber-aware manual-reset events and FIFO,
  nonrecursive mutexes. Waiting parks a child back to its main continuation;
  the main continuation cannot park. Applications choose their dispatch policy.
- Optional `KFIBER_ATTACH_MATH_CONTEXT` preserves the 16 XMTRX registers across
  cooperative transfers. Attachment fixes the policy for that owner thread.
  It uses a 64-byte aligned allocation per fiber/main context and allocates
  nothing while switching. FPSCR and FPUL are not made fiber-private: temporary
  FPU modes must be restored before transferring or returning.

## Kernel integration and ordinary-thread cost

The public `kthread_t` layout is unchanged. Fiber metadata is allocated only
when attaching the runtime or creating fibers; no worker thread is started.
The runtime lazily registers an allocation-free stack resolver. Scheduler and
unwinder paths consult it only when the stack pointer is outside the normal
thread stack. The scheduler uses the logical SP accessor from the prerequisite
patch so soft-gUSA restart markers are not mistaken for stack addresses.

The extra upper-bound comparison is a real common-path change; this proposal
does not claim literally zero instruction overhead for ordinary scheduling.
Verify that a linked non-fiber application does not retain the fiber runtime
before claiming unused code has no application footprint. Generic kernel
export tables may have different retention behavior from normal static links.

## Service executor boundary

The executor will be a separate addon consuming this core. It currently relies
on `_fiber_set_wait_observer` and `_fiber_cancel_wait` through the private
`fiber_internal.h`. Those hooks stay private in this initial proposal; they are
not a supported addon ABI yet. Before extracting the executor, agree a minimal
documented notification/cancellation interface with lifecycle and IRQ rules.
Do not install the entire private header or export every underscored helper
merely to make the addon compile.

## Review and validation

Core examples included here:

- `basic/threading/fiber-context-probe`: saved registers, FR12, stack transfer,
  and scheduler preemption.
- `basic/threading/fiber`: lifecycle, main/child switching, shared TLS/MMU
  identity, callbacks, return, IRQ exclusion, and store-queue exclusion.
- `basic/threading/fiber-sync`: sticky events, FIFO mutex handoff, recursive
  and dispatcher deadlock rejection, and object lifetimes.
- `basic/threading/fiber-math`: opt-in XMTRX preservation for the main
  continuation and two children, using only core APIs.

The integrated fork's historical emulator results are not results for this
extracted branch. Fresh clean builds, linked-symbol/export checks, and emulator
runs are required. Physical Dreamcast tests remain open. A future MMU-on probe
must be checked against upstream's MMU/SQ implementation rather than silently
bringing in unrelated memory-management changes from the fork.

API/lifecycle review should explicitly cover thread exit while child fibers or
cooperative objects exist, cancellation while parked, shared TLS/errno behavior,
and FPU mode assumptions. No media playback support or throughput is implied.

### Extraction validation, 2026-09-23

- Full KOS build passed using SH-4 GCC 16.2.0 on this isolated upstream-based
  tree, including regenerated kernel export stubs.
- All four core fiber examples, normal `hello`, and the prerequisite
  `irq-stack-test` target probe linked successfully.
- All 22 public fiber exports have definitions and generated stubs.
- Linked public fiber examples contain the production architecture switch and
  no service-executor or SH4ZAM symbols. The normal `hello.elf` contains no
  `fiber_*` or `arch_fiber_*` symbols.
- The `kthread_t` definition is byte-for-byte identical to upstream's.
- Changed core C translation units also compiled with `-Werror`.
- Prerequisite saved-stack host tests passed GCC 14 GNU17/C23 and Clang
  address/undefined-behavior sanitizers, in soft-gUSA and plain configurations.

These are build, link, header-test, and symbol checks. Fresh emulator runs of
the extracted fiber runtime and physical-hardware tests are still pending.
