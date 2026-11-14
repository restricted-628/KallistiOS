# Resident sequence publication fixture

Rebuild KOS with the ARM7DI compiler before linking this example. It requires the
sequence-resource feature and refuses unsupported prebuilt firmware. The local
builder authors original ASQ data; no game/SDK content is required.

The fixture uploads two programs into two independently owned resources, checks
whole-bank readiness and query counts, drops both and verifies allocator recovery
for two generations. It also cancels an upload and waits for native transfer
retirement before verifying recovery. It starts no voices or sequence playback.

PASS/FAIL leaves the application alive. Runner termination is not a sound-system
shutdown proof. Success qualifies this CPU-owned, unplayed resource transaction;
it does not prove physical sample-fetch retirement or audible music behavior.
