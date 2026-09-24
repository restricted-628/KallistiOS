# Direct raw PIO transport regression

Compiles the production direct driver with MMIO spies instead of real register
access. The normal driver build retains direct volatile accesses. This probe
runs the packet/data/status state machine, including multi-phase reads, exact
byte counts, underrun rejection, overflow draining with destination guards,
input validation, and release of G1. It also checks that DMA rejects odd raw
counts and that cooked ranges and staged sessions reject the raw format.

Large cooked/raw reads check FAD, buffer offsets, and command lengths across
16-sector boundaries, including 17/32/33-sector requests and odd raw tails.
A simulated clock checks decreasing lock budgets and expiration between
commands, without sleeping. Partial transfers retain aggregate byte counts;
short/oversized final commands and timeout leave the destination guards intact.

No BIOS sector mode or real disc is required. This is a software transport
regression, not proof of physical raw-sector contents, timing, recovery, or
DMA support. A real-drive comparison remains required before hardware claims.
