# Service Executor, workqueues and deferred events

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Fibers/services**.

## Scope and entry points

Optional service execution, thread workers, cancellation/drain and deadline events.

- `include/kos/fiber_service.h`
- `kernel/thread/fiber_service.c`
- `include/kos/workqueue.h`
- `kernel/thread/workqueue.c`
- `include/kos/timer_event.h`
- `kernel/thread/timer_event.c`

Relevant existing documentation (dated claims must be rechecked):

- `doc/background-execution-audit.md`
- `doc/workqueue-safety.md`

## Preserve these boundaries

- Do not silently move blocking or arbitrary callbacks onto a shared cooperative carrier; preserve observable stack, priority, TLS and head-of-line behavior.
- Specify queue membership, running-callback ownership, requeue suppression, cancellation, self-drain and teardown ordering.
- Workqueue mutex APIs are not hard-IRQ ingress; use the subsystem's bounded event/wakeup mechanism.
- Optional services must not impose startup threads or permanent memory on applications that do not select them. Networking and direct I/O must not require the executor.

## Coordinate before changing

- Threads/fibers
- Clocks/VBlank for deadlines
- Every subsystem with producers or callbacks

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/workqueue-test` — candidate `make -C utils/workqueue-test test` in an assigned checkout.

Target/example directories (source the assigned checkout's environment and inspect its Makefile before building):

- `examples/dreamcast/basic/threading/workqueue-safety`
- `examples/dreamcast/basic/threading/timer-event`
- `examples/dreamcast/basic/threading/fiber-service-sync`
- `examples/dreamcast/basic/threading/fiber-service-queue`

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

A queued cancellation is not proof that a callback has retired.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
