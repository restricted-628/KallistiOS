# Checked DSP control

This example exercises the safe, content-independent part of the checked AICA
DSP API. It initializes and validates a caller-owned program image, inspects
the coherent driver state, changes one silent effect return, verifies the
copy-out, and clears the DSP with a bounded command-queue drain.

The example deliberately does not load its synthetic program. A useful DSP
program is authored effect content, while an arbitrary nonzero instruction
sequence is not a sensible hardware demonstration. Program-image validation
is covered independently by the host test in `utils/snd-dsp-test`; projects
with real effect content use the same description with
`snd_dsp_program_load()`.

No DSP thread, service, or periodic callback is created by this example or by
the API. A green PASS or red FAIL screen remains visible for emulator and
hardware testing without a serial console.
