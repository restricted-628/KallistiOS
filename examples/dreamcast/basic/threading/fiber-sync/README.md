# Cooperative fiber synchronization probe

This regression verifies manual-reset events and FIFO, nonrecursive cooperative
mutexes on one attached KOS thread. A contending child fiber parks back to the
main fiber, while a contending main fiber is rejected because it cannot park
without losing the dispatcher continuation.

The probe also checks ownership transfer, waiter state, cancellation-safe object
lifetime, recursive-lock rejection, and that clearing an event does not revoke a
fiber which was already made ready.

Success prints `KOSFIBERSYNC sequence=8 fifo=2`.
