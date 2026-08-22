# Fiber service executor probe

This regression validates the KOS fiber service executor. Three persistent
fibers share one owning KOS thread:

- one is woken from a VBlank interrupt;
- one wakes at an absolute monotonic deadline;
- one receives a cooperative shutdown request while waiting indefinitely.

Interrupt handlers only publish a wake and signal the owner thread. They never
switch a fiber or alter the owner thread's MMU context.

The probe also attempts a service wait while that service owns a store-queue
transaction. The rejected transfer must return `EBUSY` without leaving the
service in a waiting state.

Success prints `KOSFIBERSVC irq=1 deadline=N shutdown=1`.
