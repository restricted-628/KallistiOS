# Fiber service mailbox probe

This regression configures a two-entry mailbox, fills it before executor
startup, verifies explicit `EAGAIN` backpressure, and consumes the messages in
FIFO order. A VBlank interrupt posts a third message without allocation and
wakes the parked service.

The next empty receive uses an absolute deadline and must return `ETIMEDOUT`;
an indefinite receive is then cancelled by executor shutdown. Queue statistics
confirm three successful posts and receives, a two-message high-water mark, and
one rejected post.

Success prints
`KOSFIBERQUEUE posted=3 received=3 rejected=1 irq=1 cancelled=1`.
