# KOS fiber and service runtime

## Ownership model

A fiber is a continuation within one KOS thread. It is not a smaller thread and
never appears in the kernel run queue.

| Resource | Owner | Fiber-switch behavior |
| --- | --- | --- |
| CPU continuation and stack pointer | Fiber | Switched |
| Borrowed stack bounds | Fiber | Resolved lazily when outside the owner stack |
| Priority and scheduler state | KOS thread | Shared |
| Kernel wait identity | KOS thread | Shared |
| MMU context and ASID | KOS thread or process layer | Shared |
| GBR and compiler TLS | KOS thread | Shared |
| KOS TLS, newlib state, and errno | KOS thread | Shared |
| Working directory | KOS thread | Shared |

The original `stack` and `stack_size` remain the only stack state stored in
`kthread_t` and the only allocation freed with the thread. A normal saved stack
pointer is validated directly against those bounds. Only a pointer outside the
owned stack consults an allocation-free resolver registered when the first
thread attaches the fiber runtime. Consequently threads which never attach
fibers carry no fiber fields and do not link the fiber runtime merely because
the scheduler supports it.

## MMU relationship

Fiber creation does not create mappings, page tables, address spaces, or guard
pages. Every borrowed fiber stack must already be mapped and writable in the
owner thread's address space for the complete fiber lifetime.

Current KOS applications normally use one process-wide MMU context. The fiber
contract is also compatible with a future per-thread address-space model:
kernel thread scheduling would switch that thread's MMU context, while all
fibers owned by the thread would continue sharing it.

Calling `mmu_use_table()` or `mmu_switch_context()` from a fiber is not a
fiber-local operation. It changes the address-space state seen by the whole
owner thread and must be coordinated by the process or scheduler layer.

## Interrupt relationship

A context transfer briefly masks interrupts while it updates the current fiber
and transfers the nonvolatile SH-4 call state. The incoming continuation is
published before its saved interrupt mask is restored, so an interrupt cannot
observe the incoming stack with the outgoing fiber identity.

Fiber switching is forbidden from interrupt context. An interrupt belongs to
the KOS thread it interrupted, including that thread's kernel context and stack
accounting. Replacing its return continuation with an arbitrary fiber would
cross the scheduler boundary and become unsafe once threads have distinct MMU
contexts.

Interrupt handlers may instead call `fiber_service_wake()`. That operation only
coalesces a wake flag and signals the executor thread. The executor performs the
actual fiber transfer later in ordinary thread context.

## Blocking and cooperative work

A general fiber may call a blocking KOS API, but the blocked entity is its whole
owner thread. Other KOS threads continue to run; sibling fibers on that thread
do not.

Service fibers therefore use `fiber_service_wait()` and
`fiber_service_yield()`. These return to the executor's dispatcher fiber rather
than entering a kernel wait. The dispatcher blocks its KOS thread only after no
service is ready, using the nearest monotonic deadline or an external wake.

Services must keep work between waits or yields bounded. Shutdown can deliver
`ECANCELED` to a suspended service and drain a cooperative service, but it
cannot safely force a service that never yields to release the executor thread.

## Service mailboxes

Each service may configure one bounded mailbox before its executor starts. The
runtime allocates the ring once during configuration; posting and receiving do
not allocate. Messages contain two uninterpreted machine words, so a subsystem
may use them as an operation tag plus integer value or borrowed pointer.

`fiber_service_post()` copies one message, reports `EAGAIN` when the ring is
full, and may run in interrupt context. An empty-to-nonempty transition wakes
the executor. `fiber_service_receive()` consumes messages in FIFO order and
parks only the service fiber while empty. Absolute deadlines report
`ETIMEDOUT`, while executor shutdown reports `ECANCELED`.

The mailbox statistics expose current occupancy, capacity, high-water mark,
successful posts and receives, and rejected full-queue posts. Producers must
stop before executor destruction, and any pointer encoded in a message remains
the producer's lifetime responsibility.

## Cooperative safe points

A public fiber transfer is legal only from ordinary thread context with
interrupts enabled and no thread-scoped hardware transaction that has inhibited
cooperative switching. `fiber_switch()` returns `EBUSY` without changing either
fiber when the calling thread owns a store queue mapping or its SH-4 interrupt
state masks interrupts. Normal scheduler preemption is unaffected.

Fiber transfers query the recursive store-queue mutex owner directly while
interrupts are masked. Ordinary threads therefore perform no fiber-specific
counter updates in `sq_lock()` or `sq_unlock()`. Sibling fibers share one
`kthread_t` and would otherwise appear to be the same recursive owner, allowing
one fiber to replace another's QACR or SQ TLB mapping. MMU enablement must also
remain unchanged from each `sq_lock()` through its matching `sq_unlock()`.

Ordinary KOS mutexes remain thread-owned and are not automatically tracked by
the fiber runtime. Applications must not yield while holding one. The separate
cooperative objects in `kos/fiber_sync.h` park only a child fiber and transfer
to its owner thread's main fiber. They do not alter existing KOS mutex behavior.

Cooperative events are sticky, manual-reset notifications. Setting one makes
all current waiters ready; clearing it does not revoke fibers already woken.
Cooperative mutexes are nonrecursive and hand ownership to parked waiters in
FIFO order. Both object types are bound to the attached runtime which created
them, although an event may be set or cleared from another thread or interrupt.

The main fiber is the dispatcher continuation. It may acquire an uncontended
cooperative mutex or observe an already-set event, but it cannot park. A wait
which would park the main fiber returns `EDEADLK`. General-purpose users must
explicitly dispatch fibers made ready by an event or mutex. A service executor
does this automatically through the same runtime wait notifications.

Code driving other hardware outside a KOS-managed transaction must likewise
finish that transaction before yielding.

## Validation gates

The examples under `examples/dreamcast/basic/threading` exercise these layers:

- `fiber-context-probe` verifies the SH-4 nonvolatile register and stack
  transfer directly, including a value held live in `FR12`.
- `fiber` verifies the public fiber lifecycle, automatic return, callbacks,
  scheduler preemption, shared TLS, unchanged MMU identity, MMU-off SQ access,
  and rejection of switches while interrupts or SQ ownership inhibit them.
- `fiber-mmu-probe` enables KOS MMU support, verifies SQ translation and
  cooperative-switch inhibition in that mode, and confirms that fibers retain
  their owner thread's MMU identity.
- `fiber-service-probe` verifies an IRQ-context wake, a monotonic deadline,
  persistent service stacks, shared executor identity, rejected-wait state
  rollback, and draining shutdown.
- `fiber-sync` verifies manual-reset event state, two-waiter FIFO mutex
  transfer, recursive and main-fiber deadlock rejection, and busy lifetime
  checks.
- `fiber-service-sync` verifies that the same event and mutex primitives park
  and wake service fibers through the executor without a parallel API. It also
  posts a mailbox message during mutex contention and issues a service wake
  during an event wait, proving that neither can forge synchronization success
  and that the unrelated wake remains pending afterward.
- `fiber-service-queue` verifies pre-start FIFO messages, bounded backpressure,
  allocation-free IRQ posting, cooperative receive deadlines, and accounting.

The emulator test harness stops after each terminal success marker. Returning
from a boot image can reboot it and is not part of the fiber regression.

On 2026-08-20, a clean GCC 16.2.0 build passed the three mode-sensitive gates
in Flycast with both the SH-4 interpreter and dynarec:

- `KOSFIBERCTX scheduler=2 bounds=2`
- `KOSFIBERAPI scheduler=2 callbacks=8`
- `KOSFIBERMMU sequence=2 sq=1`

The same clean build also passed the higher-level interpreter-mode gates:

- `KOSFIBERSYNC sequence=8 fifo=2`
- `KOSFIBERSVC irq=1 deadline=50 shutdown=1`
- `KOSFIBERSERVICESYNC sequence=6 isolated=2 preserved=1`
- `KOSFIBERQUEUE posted=3 received=3 rejected=1 irq=1 cancelled=1`

The strengthened `fiber-service-sync` isolation and pending-wake gate also
passed independently with dynarec enabled.

Changes to `kthread_t` require a complete KOS kernel rebuild before testing.
The makefiles do not currently guarantee that an incremental build recompiles
every object which consumes the structure layout; mixing objects from two
layouts corrupts scheduler state and is not a valid runtime result.
