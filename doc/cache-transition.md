# Cache-mode transitions

`cache_write_ccr(mask, value)` retains the configuration formula
`(old & ~mask) | value`. It now blocks exceptions before reading CCR and runs
the register transaction in P2. This closes the read/modify/write window in
which an interrupt could previously change the configuration after the read.

Retirement follows the **old** ORA setting. With OCRAM disabled, all 512
operand-cache entries are retired. With OCRAM enabled, entries 128-255 and
384-511 belong to scratchpad storage and are excluded from the address-array
scan. Non-associative zero-tag writes retire dirty valid cache lines before
the new mode takes effect. The final CCR write includes OCI to clear all U/V
tags after retirement, including tags formerly associated with scratchpad.
OCI is self-clearing; it is not an extra persistent mode bit.

This follows the SH7750/SH7750S layout in the public
[Renesas hardware manual, sections 4.2, 4.3.6 and 4.5.3](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
The SH7750R scratchpad layout is different and is not the Dreamcast target.
The manual describes OCI as a tag operation, not a scratchpad-data clear.
The old helper's writes to scratchpad-associated **tags** were not demonstrated
to corrupt scratchpad **data**; this change does not claim that failure.

## Caller responsibilities

- Quiesce DMA and all cache/scratchpad-workspace users before changing modes.
  CPU exception exclusion is not device synchronization.
- ORA requires OCE. The raw helper does not validate or repair mode choices.
- OCRAM contents remain usable only while ORA stays enabled. Clearing ORA
  relinquishes storage; enabling it again does not restore previous contents.
- OIX changes scratchpad bank addressing. With OIX off, 0x7c001000 and
  0x7c002000 address the two 4 KiB banks; with OIX on, use 0x7c001000 and
  0x7e001000. Do not keep using an unchanged contiguous 8 KiB pointer.
- Supply ICI for instruction-cache transitions as required; the existing
  `icache_toggle_icindex()` does so. This helper adds OCI, not automatic ICI.
- Retirement may write back dirty lines through their existing physical tags.
  This does not make arbitrary SDRAM aliases safe on 32 MiB modifications.

There is no heap, stack, FPU, or new inline-assembly memory clobber in the
transition helper. It does not change startup mode policy or acquire leases.

## Validation

Run `make -C utils/cache-transition-test`. The limited register/MMIO model
executes the production helper and checks old-layout retirement, dirty-line
ordering, exact requested bits plus OCI, one protected CCR read/write, full
tag invalidation, SR/callee-register preservation, and return spacing. Built-in
negative controls reject missing exclusion, missing OCI and incorrect old-mode
selection. It is not a cycle-accurate CPU or cache model.

The standalone `examples/dreamcast/basic/cache-transition` probe checks dirty
ordinary RAM and both OCRAM banks across mode changes. It takes ownership of
all scratchpad storage and must not run alongside other OCRAM users. A passing
emulator run is only an emulator regression result. Physical Dreamcast tests
remain necessary for scratchpad retention, bus/cache behavior and timing.
