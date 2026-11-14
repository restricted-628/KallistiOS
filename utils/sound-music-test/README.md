# Private resident music core

Run `make test sanitize` here. These tests compile the actual ARM-side music
engine, original ABK v1 resolver and bank registry for the host. They exercise
pitch accuracy and monotonicity, eight ports and sixteen channels, all 64 voice
slots, FIFO backpressure, stale generations, sustain, mailbox, controls/fades,
priority exhaustion/stealing, start failure and conservative bank references.
They do not exercise MMIO, establish ARM timing, or prove audible output.

The engine is caller-serialized and receives an exclusively reserved physical
channel pool through callbacks. MIDI messages enter a 64-entry FIFO; each service
pass handles at most four events and sixteen periodic voice callbacks. A bank
lookup visits at most 32 zones plus one sample record per call. A pending lookup
keeps its producer reference until rejection or acquisition of the voice lease.
Immediate port controls are explicitly outside that FIFO; a transport must define
its command ordering. Closing a port invalidates queued events by generation.

Native policies:

- Eight ports, sixteen MIDI channels per port and one shared pool of 1–64 voices.
- Note on/off, velocity, program, bank controller 32, channel volume/pan/expression,
  sustain, modulation depth, pitch bend and RPN bend range. GM uses channel ten
  for drum zones. Controller 48 writes a per-port mailbox. Pressure and other
  unsupported controllers are no-ops. This is not a complete General MIDI synth.
- Port-relative volume/pan/pitch fades use elapsed milliseconds; direct/effect
  offsets are immediate. Zero offsets preserve authored values. Pitch uses
  256 units per semitone and an original integer equal-temperament calculation.
- Voice choice: idle first; a nonzero incoming priority cannot steal a higher
  priority. Eligible released voices precede held voices, then lower priority,
  older age, and lower slot index. Priority zero bypasses the eligibility filter.
  Gains and tie rules are native policies, not vendor bit-exact emulation.
- ABK v1 resolves one non-overlapping key/velocity zone. Layered instruments,
  sequences and the firmware/SH4 transport are separate integration work.

Every used channel/bank pair holds an idempotent voice reference. Note-off,
logical completion, stealing, mute, close and quiesce **do not** release it.
Consequently a played bank stays busy until a separately qualified physical
retirement operation exists. Reinitializing live state to erase references is
invalid. No delay, key-off bit or logical completion is a sample-memory fence.
A failed START consumes its token, stops event processing and retains the lease;
quiesce also requests a stop for that possibly touched physical channel.
