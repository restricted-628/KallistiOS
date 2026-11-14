# Private original AICA DSP authoring

This utility creates the existing KOS `snd_dsp_program_t` image. It does not load
hardware, change the DSP owner, replace firmware or write live coefficients.
Compile `dsp_build.c` with the native `snd_dsp_program.c` validator. The builder
runs on SH-4 (or a host); its output is AICA DSP microcode, not ARM/SH-4 code.

`aica_dsp_encode(step, fields, instruction)` validates all full-width fields and
packs four16-bit words in existing KOS address order. Sources0..31 are MEMS,
32..47 are MIXS,48..49 are EXTS. All sixteen effect returns are available.
Memory controls require an odd zero-based step, including TABLE, NOFL, MASA,
ADREB and NXADR even if read/write bits are clear. Encoding does not schedule
pipeline hazards, prove a work-memory extent, or initialize referenced state;
validate the complete image with `snd_dsp_program_validate` before loading it.
An all-zero instruction performs arithmetic; it is not a safe general NOP.

`aica_dsp_coefficient` converts a signed Q12 integer in[-4096,4095] to the native
13-bit coefficient in bits15:3. Low bits remain zero. No saturation, floating
point or hidden rounding occurs. Positive exact unity is not representable;
4095 means4095/4096. All helpers leave the output unchanged on invalid input.

`aica_dsp_build_gain(program, routes, count)` makes a complete gain/router image
for0..16 routes. Each route names an input, a unique output and signed Q12 gain.
One multiplication step precedes its output write, honoring the DSP's accumulator
latency. Unused returns are explicitly zeroed every sample. Zero routes creates a
silent image. All idle arithmetic uses a zero coefficient and ZERO input, so it
does not depend on the previous TEMP/FRC contents. There are no memory, TEMP,
MEMS or latch writes. Existing KOS loading still reserves its minimum16KiB work
area even for this memory-free program. Routes may read MEMS, but the caller must
provide those values through its separately coordinated live-input contract.

Example preparation (MIXS0 to return0 at half gain):

```c
snd_dsp_program_t image;
aica_dsp_gain_route_t route = {32, 0, 2048};
if(aica_dsp_build_gain(&image, &route, 1) == 0) {
    /* Use the existing DSP load/output API under its ownership and
       no-routed-playing-channel admission contract. */
}
```

The utility changes no live-output level or pan. These remain separate output
controls. There is no implicit safe coefficient-update interval, crossfade,
hardware retirement delay or SDK effect-bank compatibility.

`make test` and `make sanitize` exercise every legal field value at all128 step
indices, malformed fields, coefficient endpoints, generated-image validation,
1024 deterministic randomized routing sets, signed clipping and repeated silence.
The small original arithmetic oracle implements only the emitted gain subset and
rejects other instructions. It is a software scheduling/numerical check, not a
complete DSP emulator, listening test or physical-hardware qualification.
