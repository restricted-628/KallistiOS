# Cooperative fiber test

This program exercises KOS cooperative fibers on two caller-owned stacks. It
checks explicit switching, automatic return to the main fiber, state and error
contracts, switch callbacks, callee-saved floating-point state, and scheduler
preemption while an alternate stack is active.

The fibers also verify that compiler TLS and the current MMU context remain
properties of the owner KOS thread rather than becoming fiber-local state.
The probe rejects transfers while interrupts are masked or a store-queue
transaction is active, then verifies an MMU-off store-queue copy.

Success prints `KOSFIBERAPI scheduler=N callbacks=8`.
