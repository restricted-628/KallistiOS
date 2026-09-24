# SPU DMA ownership regression

This probe uses the real SPU request workers and callback dispatcher, with
link-time wrappers replacing only G2 submission/status/cancellation. The fake
engine refuses stop requests until the probe explicitly completes it. Both
caller cancellation and execution-deadline expiry must retain the request and
borrowed buffer until that completion; destroying an active request must fail.

Build with the normal KOS environment and run `spu-lifetime.elf`. Success prints
`SPU-LIFETIME: PASS cases=2 callbacks=2`. This is a scheduling/lifetime test,
not physical DMA cancellation or sound-RAM validation. For actual data movement
use `spu-transfer` and the generic `g2-state` example.
