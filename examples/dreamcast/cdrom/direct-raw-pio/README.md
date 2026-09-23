# Direct raw PIO transport regression

Compiles the production direct driver with MMIO spies instead of real register
access. The normal driver build retains direct volatile accesses. This probe
runs the packet/data/status state machine, including multi-phase reads, exact
byte counts, underrun rejection, overflow draining with destination guards,
input validation, and release of G1. It also checks that DMA, cooked ranges,
and staged sessions reject the PIO-only raw format.

No BIOS sector mode or real disc is required. This is a software transport
regression, not proof of physical raw-sector contents, timing, recovery, or
DMA support. A real-drive comparison remains required before hardware claims.
