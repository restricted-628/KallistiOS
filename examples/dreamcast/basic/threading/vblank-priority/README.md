# VBlank priority regression

Registers interleaved legacy and explicit-priority IRQ callbacks, then records
two frames. Lower numbers run first, explicit ties run newest first, and legacy
callbacks retain FIFO order at priority 128. One callback removes itself.
Recording is armed only after all registrations have succeeded.

Expected marker: `VBLANK-PRIORITY: PASS events=13 ties=432 legacy=56 removal=0`.
Callbacks only record state; they never block or allocate in the IRQ.
