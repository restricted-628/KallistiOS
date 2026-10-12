# Fiber context probe

`make` builds two separate diagnostics. Run each ELF independently.

`fiber-context-probe.elf` retains the example-local SH-4 cooperative switch.
It transfers between two caller-owned stacks, checks the sequence and an fr12
cookie, and verifies automatic return to the main context.

Interrupts remain enabled while each alternate stack is running. Both fibers
force scheduler passes to a helper KOS thread and count continuation-stack
bounds queries. Only the short bookkeeping and context-transfer window is
masked. This low-level diagnostic does not directly check MMU/TLS identity
or exercise the public fiber runtime.

Success prints `KOSFIBERCTX scheduler=N bounds=N` and exits with status zero.

`fiber-runtime-probe.elf` uses the actual core provider with
`KFIBER_ATTACH_MATH_CONTEXT`. Two children have distinct XMTRX patterns; the
main fiber and a competing KOS thread load two further patterns. It checks:

- Helper progress while each child spins without yielding, with a two-second
  observation limit; the helper inspects the saved logical child-stack pointer
  and records raw r15 separately from the logical SP.
- Carrier resumption after preemption, `thd_pass()` and `thd_sleep()`, plus
  cooperative yield/resume and automatic child return.
- Shared carrier identity: GBR, compiler TLS address/value, KOS TLS value,
  newlib pointer and errno address/value; the helper has separate thread state.
  The sleep check expects current genwait's EAGAIN timeout side effect, then
  resets the carrier's errno cookie before cooperative transfers.
- Restored SR MD/RB/BL/FD/IMASK bits, r8/fr12 cookies, and all 16 XMTRX words.
  T/Q/M flags are ABI-clobbered and are not compared across C calls. The probe
  does not create a separate per-fiber FPSCR/FPUL environment.

Run with `make run-runtime`. Success prints
`KOSFIBERRUNTIME core preempt=N,N failures=0x0 PASS` and exits zero. Failures
are a bitmask: identity 1, SR 2, stack 4, registers 8, matrix 16, missing
preemption 32, API/state 64. A fatal exception/hang still requires the runner's
external timeout. The link map identifies the provider actually linked.

This is a core-provider diagnostic using KOS matrix operations. It does not
validate the exclusive SH4ZAM addon provider or provider exclusivity; that
provider needs its own direct-SH4ZAM build and execution. Never link both.

Ordinary successful context switching cannot establish that live SR.BL was
set before restoring SPC/SSR inside the exception-return window. Use the
Kernel's separate return-guard diagnostic for that observation. This probe
does not inject an exception into that window, force a soft-gUSA interruption,
test alternate MMU mappings, or establish NMI/UBC/nested-exception safety.
Target linking, emulator execution and physical-hardware execution are
separate validation results.
