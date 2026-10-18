# Resident pin probe

Build with a configured KOS environment using `make`. This opt-in probe requires
working SH-4 MMU translation. It reuses the resident-vm preflight on separately
owned backing before creating managed data; it does not preload managed pages.

Pass requires exactly `VM3PIN RESULT failures=0 translation=1 geometry=6 yields=2
abandoned=1 transfers=2` on one line, with no CHECK/setup/panic errors. It checks
exact byte-run coverage against page-table coordinates and translated contents,
retention across explicit fiber suspension, cleanup after abandoned child
execution, and publication/release across joined carrier threads. Test stacks
and token ownership records remain directly resident. Provider selection is the
build's responsibility; never mix the core and addon bundles.

No P0 buffer or synthesized alias is submitted to DMA. Translation and software
lifetime observations are not cache/device/hardware proof. A host process exit
or incomplete result is not a guest pass; hardware validation remains separate.
