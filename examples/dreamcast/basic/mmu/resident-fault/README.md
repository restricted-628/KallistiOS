# Resident fault probe

Build with the checkout's normal Dreamcast KOS environment and `make`. Requires
an MMU-capable runtime honoring ordinary MMUCR.AT activation and real P0 misses.
Do not use SQ-only execution or manually seeded VM pages as recovery evidence.

Success requires the exact final line:

```
VM2REC RESULT failures=0 cases=32 state=32 zero=16 guard=16 child=16 observer=16
```

Also require all 32 distinct CASE rows with zero failures and no CHECK, SETUP
FAIL or panic. A process exit or partial row set is not a pass. The probe covers
every combination of carrier/child stack, absent/observational global hook,
zero-commit/guard, ordinary/delay-slot read/write. Reads use read-only tickets;
writes use read-write tickets. It checks one-shot snapshots, EBUSY and masked
API rejection, table/ASID, carrier/compiler/KOS TLS/reentrancy/errno, and all
integer/special/FPU registers across the access. The intentional load result is
the only expected register difference. The assembly fixture restores its entry
state before returning to C and never substitutes for kernel exception entry.

The two FPU banks include secondary XMTRX contents. No fiber switch occurs
inside the measured interval. A child stack is direct resident memory outside
the carrier's main stack. Existing fiber-context and fiber-MMU probes remain
separate scheduling/provider regressions. This fixture is single-carrier and
not reentrant; its global snapshots are deliberate test storage.

This current-worker probe does not certify a different complete provider bundle,
physical cache behavior, nested faults or fatal-path/alignment conformance.
Keep provider symbols, compiler/pins and ELF identity with results. Hardware
and each complete exclusive provider require their own validation.
