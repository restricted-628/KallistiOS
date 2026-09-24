# Translated cache-workspace regression

Build with `make` in a configured KOS environment. No automatic run target.
Requires default MMU startup, no existing context, and OIX/ORA initially off.
This standalone program owns its context and CPU-wide OIX transitions.

Checks normal and OIX cache modes, virtual index different from physical index,
whole-cache writeback/purge, and dirty-page retirement on unmap, cache-policy change, replacement and
inactive-context destruction. Uncached readback verifies every word after
retirement. A first-touch TLB miss occurs inside a leaf function and must preserve
its return address. The 32 MiB variant allocates a large owned heap span so the
upper-bank case really resides above physical `0x0d000000`; it does not borrow
arbitrary RAM or use physical A25 aliases. Expected case counts: 2 on 16 MiB,
4 on 32 MiB.

An emulator that does not apply general P0 translations prints UNAVAILABLE and
does not pass. Even a cacheless emulator with translation cannot establish
cache writeback or electrical behavior. Real cache/MMU and stock/modded-console
tests are required. Do not interpret the compiled test as a compatibility fix
for existing binaries or as proof about any specific hardware failure.
