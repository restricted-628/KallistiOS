# PVR pipeline status

This example renders a green triangle while checking coherent pipeline-state
snapshots at deterministic API boundaries. It verifies:

- an initialized, idle snapshot;
- active-scene and open-list transitions;
- a monotonic transition sequence;
- an empty persistent fault record during normal rendering;
- checked failures for a null destination and an invalid fault mask.

The expected image is a green triangle on a black background. The program
aborts if a contract check fails. On success it prints
`RESULT: PASS (PVR pipeline status)`.

Emulation checks the software state machine and normal interrupt flow. It does
not validate physical fault timing or recovery.
