# Saved stack pointer regression

The scheduler must check the logical stack address, not GCC soft-gUSA's
temporary negative length in r15. The real stack remains in r1 until the region
end restores it. Neither the marker nor the saved PC may be changed by the
stack-address accessor: IRQ restart and exception return still need them.

`make test` compiles the actual architecture header twice, with and without
soft-gUSA enabled. Cases cover every marker from -128 through -1, both the
region interior and its end, ordinary P1/P2 pointers, invalid non-marker values,
an invalid preserved r1, and byte-for-byte context preservation. The small host
shim supplies only generic IRQ declarations; the tested helper is not mocked.
The suite participates in `utils/run-host-tests.sh` GNU17 and strict C23 lanes.

After sourcing the KOS environment, `make dreamcast` builds an ELF that also
runs atomic fetch-add and compare/exchange under a temporary 1000 Hz scheduler
tick for two seconds. It restores the prior tick rate and global IRQ observer.
The observer checks the interrupted context without changing it or suppressing
normal per-IRQ dispatch. The final atomic value must match the operation count.

Inspect `interrupted-atomics` in the serial log. A positive count demonstrates
that interrupts actually landed in restart regions. A zero count can occur
with emulator-optimized atomics and is not proof of that path. No physical
hardware result is implied by an emulator run.
