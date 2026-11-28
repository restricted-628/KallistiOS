# Fiber Service Executor lifecycle regression

The probe queues work before start and wraps thread creation to force a
scheduler turn if the new carrier can run before its handle is stored. It
also checks that an individual stop request immediately rejects new posts
while preserving an already accepted message, and that detached attributes
are rejected before allocation. The same executor then starts with joinable
attributes and is destroyed normally.

The probe also forces the second fiber creation to fail. One retry confirms
that an accepted wake and mailbox message survive, and another injects stop
immediately after the first fiber is freed during rollback. The stop must
avoid the retired fiber, remain effective on retry, and preserve the original
startup error even if cleanup changes errno.

Success prints
`KOSFIBERLIFECYCLE dispatch=1 stop=1 detached=1 retry=1 retire=1`.
