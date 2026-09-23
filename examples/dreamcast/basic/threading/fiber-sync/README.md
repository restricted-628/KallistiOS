# Cooperative fiber synchronization probe

This regression verifies manual-reset events and FIFO, nonrecursive cooperative
mutexes on one attached KOS thread. A contending child fiber parks back to the
main fiber, while a contending main fiber is rejected because it cannot park
without losing the dispatcher continuation.

The probe also checks ownership transfer, waiter state, cancellation-safe object
lifetime, recursive-lock rejection, and that clearing an event does not revoke a
fiber which was already made ready. Both attached and unattached foreign threads
must receive `EXDEV` when waiting on either a set or an unset event. The owner's
main fiber may observe a set event, but cannot park on an unset one.

Cancellation tests destroy the first of two parked event/mutex waiters and
overwrite its caller-owned stack before waking the remaining waiter. The mutex
test also verifies that a ready handoff recipient cannot be destroyed before it
resumes and releases ownership.

Success prints `KOSFIBERSYNC lifetime foreign=4 cancelled=2 resumed=2` followed by
`KOSFIBERSYNC sequence=8 fifo=2`.
