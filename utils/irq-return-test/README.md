# Exception-return guard regression

With this checkout's KOS environment active, run `make test` to assemble the
production entry code and check emitted branch routing, BL|RB mask, and saved
SPC/SSR loads. Three negative controls reject the old defect and either half-fix.
This runs host Python and the cross assembler; it does not execute SH-4 code.

`make dreamcast` builds a separate diagnostic ELF. Its generated entry object
observes live SR immediately before the first SPC restoration. Production
entry.s and libkallisti.a receive no instrumentation. The observer uses only
scratch R0-R2, no stack/calls/FP/status writes. The link map must show entry-probe.o
providing `_irq_force_return`, with no archive entry.o in the link.

The target probe checks a no-switch trap (exact saved/returned SR and SP),
voluntary yield, first activation and timer preemption while a child spins.
Every observed return must have BL|RB set; thread execution must restore BL/RB
clear. Separate thread TLS and errno patterns are checked. Run serially on an
assigned emulator/console; building the ELF does not constitute a run.

For a negative runtime control, generate the diagnostic from the original
unguarded entry source in an isolated build. Ordinary switching may succeed,
but the live-SR assertions must fail. Do not inject NMI/UBC or claim that this
probe reproduces a physical exception inside the vulnerable window.

Full GPR/FP-bank, child-fiber-stack and alternate-provider coverage belongs to
the separate context probes/interface review. The observer changes timing;
this test does not establish latency, nesting safety or hardware certification.
