# Fiber context probe

This validation program exercises KOS's production SH-4 cooperative context
switch directly, using example-local dispatch bookkeeping rather than the
public fiber runtime. It transfers between two caller-owned stacks, checks
callee-saved integer and floating-point state, and verifies automatic return
to the main context. It does not carry a second copy of the switch assembly.

Interrupts remain enabled while each alternate stack is running. Both fibers
force scheduler passes to a helper KOS thread, validating that preemption,
resume, lazily resolved continuation-stack bounds, and the thread-owned MMU/TLS
identity remain coherent. Only the short bookkeeping and context-transfer
window is masked.

Success prints `KOSFIBERCTX scheduler=N bounds=N` and exits with status zero.
