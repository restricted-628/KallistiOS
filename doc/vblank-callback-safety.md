# VBlank callback ordering and removal

KOS multiplexes the beginning-of-VBlank interrupt across registered handlers.
This topic adds deterministic priority ordering and makes callback removal safe
while the list is being dispatched. It creates no thread, periodic activity, or
permanent buffer beyond the existing VBlank interrupt.

## Ordering

`vblank_handler_add_prio()` accepts priorities from 0 through 255. Lower values
run first. Handlers registered at the same priority retain registration order.
The legacy `vblank_handler_add()` remains available and uses priority 128.

Registration allocates one small handler record and therefore remains a
thread-context operation. A null callback fails with `EINVAL`; interrupt-context
registration fails with `EPERM`.

## Mutation during dispatch

A callback may remove itself or another handler. Removal marks the target
inactive while interrupts are disabled, so a later callback removed during the
same dispatch is skipped. The list node remains valid until traversal has
finished, preventing use-after-free of either the current node or its successor.

Heap reclamation never occurs in the VBlank interrupt. Removed records are
detached and freed by the next thread-context add/remove operation, or by
VBlank shutdown. If no later thread-context operation occurs, the bounded
records remain inactive until shutdown.

Removal is also safe from another interrupt context because it only marks the
record; reclamation follows the same thread-context rule. Callbacks themselves
remain ordinary interrupt handlers and must not block, allocate, or perform
unbounded work.

## Validation

`examples/dreamcast/basic/threading/vblank-priority` verifies priority order,
stable equal-priority order, self-removal, and continued dispatch. The host-side
VBlank list test executes the production implementation with instrumented
allocation, verifying that self-removal and deferred reclamation never call the
allocator from interrupt context.
