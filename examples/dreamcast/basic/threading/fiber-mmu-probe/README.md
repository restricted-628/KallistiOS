# Fiber MMU and store-queue probe

This regression enables the KOS MMU, attaches the calling thread as a fiber,
and transfers to a caller-owned fiber stack. Both continuations must retain the
same MMU context.

While the child fiber owns a store-queue transaction, a fiber transfer must be
rejected with `EBUSY`. After releasing the transaction, the probe performs an
MMU-on store-queue copy and completes two round trips between the fibers.

Success prints `KOSFIBERMMU sequence=2 sq=1`.
