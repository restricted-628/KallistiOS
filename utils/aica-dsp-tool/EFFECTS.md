# Original delay, reverb and modulation preparation

These builders emit the existing native DSP image and do no hardware I/O. Compile
`dsp_effects.c` and `dsp_build.c`, then use the established DSP loader/output APIs.
They reserve the complete program, TEMP, MEMS and ring through that existing owner;
they cannot be overlaid on another program or updated concurrently with playback.
Failed preparation leaves the destination image unchanged. No live parameter
transaction or click-free transition is provided by the builder.

## Delay / parallel feedback combs

`aica_dsp_build_delay(image, settings)` accepts1..4 parallel lines, an input32..49
(MIXS0..15 or EXTS0..1), one return0..15, a ring selector and dry gain. Each line
has a delay in samples, signed Q12 feedback strictly between-1 and1, and signed
Q12 wet gain. Dry/wet coefficients are in[-4096,4095]; exact positive unity is
not encoded as an invalid coefficient. Each line writes the input at exact unity
plus its feedback tap, using two arithmetic steps and accumulator headroom.

Work memory uses **linear PCM16 (NOFL=1)**. Initial RAM must be zeroed, as the
current KOS loader does. A ring contains8192,16384,32768 or65536 words. The ring
is divided into `count` moving segments; each delay must be at least1 and less
than `ring_words / count`. This spacing prevents another line from overwriting
history before its tap reads it. The loader owns the full ring. Ring size is a
memory budget; it is not the delay parameter. At44.1kHz,4410 samples is100ms.

For each line, the logical recurrence is:

- Read its delayed PCM16 value and expand to24-bit DSP units.
- Store the clipped/truncated input plus delayed value times feedback.
- Add the delayed value times wet gain to a saturated24-bit wet sum.
- After all lines, add dry input gain and write the selected16-bit return.

All other returns are cleared every sample. Feedback below unity bounds the
linear recurrence; integer quantization may retain small limit cycles. Clipping
may occur with high input/feedback or combined wet/dry levels. This is deliberate
saturating fixed-point behavior, not a floating-point or bit-transparent effect.

The schedule requests each read on an odd step, writes MEMS two steps later and
uses it on a following step. The write consumes the preceding accumulator while
forming the wet tap. TEMP0 stores the wet sum; MEMS0..count-1 hold delay taps.
No sample-memory access leaves the declared ring and no live address changes are
needed. The builder itself does not prove physical bus timing or retirement.

Example wet echo (100ms at44.1kHz, half feedback, half wet):

```c
aica_dsp_delay_t echo = {
    32, 0, SND_DSP_RING_16K, 0, 1, {{4410, 2048, 2048}}
};
snd_dsp_program_t image;
int result = aica_dsp_build_delay(&image, &echo);
```

`aica_dsp_build_reverb(image, input, output)` supplies an original four-comb,
wet-only preset using a32KiB ring:1493/2111/2633/3251 sample delays, feedback
2750/2680/2620/2550 in Q12, and each wet gain512. It is a modest parallel-comb
reverb with no diffuser or damping filter, not a recreation of a vendor room
preset. Delay times scale with actual DSP sample rate; no host rate is assumed.

## Amplitude / ring modulation

`aica_dsp_build_modulation(image, carrier, modulation, output)` accepts two native
input addresses0..49. It latches the modulation signal into Y_REG, multiplies on
the next step with the signed13-bit Q12 value, and emits on the following step.
A unipolar control signal gives tremolo; a bipolar signal gives amplitude/ring
modulation. The caller supplies the control input, for example through an owned
slow waveform channel routed into a distinct MIXS bus. This builder creates no
oscillator, host thread, coefficient-update schedule, pitch shifter or chorus.
Other returns are zeroed; no TEMP/MEMS/sound-memory writes are emitted.

## Validation boundaries

`make test` / `make sanitize` also run an original selected-instruction model and
an independent per-line sample-delay recurrence. Tests cover all four ring sizes,
1..4 lines, ring wraps, maximum delay, signed feedback, clipping, zero history,
line isolation, preset equivalence, invalid parameters and modulation sources.
The model checks read/IWT/accumulator scheduling and rejects unsupported fields.
It is **not** hardware execution or a complete DSP emulator.

The retained Flycast DSP interpreter ignores NOFL linear mode. Its current output
cannot qualify these delay/reverb images. Actual DSP execution/capture and physical
Dreamcast audio/timing remain required; the native image validator passing does
not close them. No emulator code or firmware was modified to conceal that gap.
