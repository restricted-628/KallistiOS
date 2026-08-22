# Store-queue safety probe

This regression checks store-queue argument validation, the complete default
eight-level recursive lock capacity, clean rejection of a ninth acquisition,
and successful copies with address translation disabled and enabled.

Success prints `KOSSQ recursion=8 validation=1 mmu=1`.

The executable must still be run on an emulator and physical hardware to
validate actual SQ write-back and MMU mapping behavior.
