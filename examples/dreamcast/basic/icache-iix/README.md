# IIX instruction-cache array regression

Build and run `icache-iix.elf`. Success reports:

`ICACHE-IIX: PASS cases=36 entries=256 restore=1`

The P2 assembly harness blocks exceptions, saves CCR, selects normal/IIX mode,
seeds all 256 IC entries, calls the production range helper through P2, reads
the resulting tags, then invalidates the IC and restores the original mode.
The C verifier checks every entry, including entries outside the range.
No code is fetched from synthetic cache contents. This is a standalone test,
not a routine to embed in a live application: it destroys cached instructions.

Coverage includes bit-12/bit-25 disagreement, misaligned endpoints, crossings
between 4 KiB halves, and sync/invalidate behavior. Sync addresses refer to the
test's ordinary allocated BSS; A25 aliases are invalidation-only. OIX, ORA and
MMU modes are not changed. The probe starts with optional fork MMU startup off.
The test does not prove instruction-fetch coherence, SDRAM mirror safety,
hardware timing or performance. See `doc/icache-iix.md` for the contract.
