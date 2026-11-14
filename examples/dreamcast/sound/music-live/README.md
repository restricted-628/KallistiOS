# Explicit experimental live MIDI fixture

Build with this checkout's ARM7DI firmware and SH4 environment, then `make`.
The build creates an original triangle bank from the native authoring fixture;
no game/SDK resources are used. Requires music feature0x80 and the matching
embedded firmware. It starts one looping note, checks logical status, quiesces,
and verifies the played bank remains retained. It reserves eight channels.

Run only in a fresh exclusively owned sound runtime, without other raw/SFX
players. No sound reset/shutdown may race this explicit owner. The test leaves
its objects and allocations alive and loops after PASS or FAIL. Terminating an
emulator process is not sound shutdown or physical retirement evidence.

The live backend intentionally has no natural-completion detection. Musical
release voices remain conservative until explicit control/stealing; no key-off
or logical stop releases sample memory or the physical channel pool. Nominal
4410-Hz serviced ticks are converted to milliseconds; this is not measured
physical timing. This fixture is not whole Manatee parity or DSP qualification.
