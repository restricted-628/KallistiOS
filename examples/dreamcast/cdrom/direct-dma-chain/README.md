# Queued direct-DMA chain regression

Compiles the production direct driver and request engine with a driver-local
clock and allocation spy, and replaces only the physical DMA request function.
The real worker queue, continuation, progress, terminal publication, callback,
cancellation, shutdown, and destruction paths run in KOS. No drive is accessed.

Covers large cooked/raw reads, exact segment sizes/FADs/buffer offsets, payload
placement and guards, partial failure, active and queued cancellation, a shared
chain timeout, callback ordering, metadata allocation failure and reclamation,
shutdown admission, and alternating segments from two queued chains. Private
segment admission rejects format/byte-count mismatches and oversized/odd raw
segments. No payload staging allocation is allowed by the driver spy.

This is software queue/ownership evidence, not physical DMA, IRQ, cache, drive
timing, payload-source, or throughput validation. The separate raw-DMA probe
exercises the production register path with simulated MMIO. Both still need
real-drive validation before hardware claims.
