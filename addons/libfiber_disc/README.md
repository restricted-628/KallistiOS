# Application fibers over direct disc DMA

Link `-lfiber_disc` and include `<kos/fiber_disc.h>`. This optional addon joins
the existing public fiber/event API to the existing asynchronous direct disc
request queue. It creates no thread, does not use private fiber hooks, does
not access hardware, and has no math or SH4ZAM dependency. Link exactly one
compatible fiber runtime; this library does not bundle a duplicate runtime.

The request worker still owns G1 and performs command, DMA, cancellation and
recovery work. Application fibers run on their caller-owned KOS thread. A read
parks only its waiting child fiber, not the thread or its siblings. The main
fiber remains responsible for scheduling; this is not an automatic scheduler
and is not a plug-in for the Fiber Service Executor's private dispatch loop.

## Owner loop and lifetimes

1. Attach the application thread and create the adapter with a live-handle
   capacity. Capacity includes streams and completed handles until explicitly
   released; a stream plus its transfer needs at least two slots.
2. Create application fibers on caller-owned stacks. A loader submits with
   `fiber_disc_read_dma` (RAM/VRAM) or `fiber_disc_read_dma_gaps` (an existing
   GAPS lease and byte offset), then calls `fiber_disc_await`.
3. The main fiber calls `fiber_disc_pump` and dispatches ready children with
   `fiber_switch`. If all children are parked, it may call the bounded
   `fiber_disc_idle`; otherwise it keeps dispatching useful work.
4. Await returns a copied terminal status, including read failure/cancellation.
   Inspect it, then release the read handle. At this point both the underlying
   request and its completion callback have finished, so buffer reuse is safe.
5. Shutdown rejects new submissions and requests cancellation. Continue the
   same pump/dispatch loop until waiters return; release read handles and
   fibers before destroying the adapter or exiting its owner thread.

The main-fiber idle call deliberately sleeps the OS thread only when the
application has no ready work. The child await never calls a kernel disc wait.
Do not use `cdrom_request_wait`, callback waits, synchronous disc reads, or
ordinary semaphore waits in a child if sibling-fiber progress is required.
Do not switch fibers while holding an ordinary KOS mutex or G1 transaction.

The callback only posts a semaphore hint. It cannot switch a fiber. The pump
samples terminal status and tries nonblocking request destruction; `EBUSY`
means the callback has not returned yet. It keeps the read/context alive and
retries. The idle path requests a 1 ms sleep between such retries, allowing a
lower-priority callback thread to finish. Only successful retirement sets the
fiber event. Early completions and completion-before-wait remain latched.
No payload buffer is allocated or copied by this adapter.

## Staged streams

Start with `fiber_disc_stream_start`, then call `fiber_disc_stream_await_ready`
from a child. Inspect the returned state: startup failure also wakes the waiter.
For a READY session, submit `fiber_disc_stream_transfer`, then use the ordinary
`fiber_disc_await` and read-handle destruction APIs before submitting the next
chunk. The direct driver retains G1 across those transfers; do not await other
queued disc work while holding the stream open.

After the final transfer, call `fiber_disc_stream_await`, inspect status, and
destroy the stream handle. To stop early, request `fiber_disc_stream_cancel`
and follow the same drain. Adapter shutdown cancels streams and reads together.
Terminal session waits also drain all adapter transfer callbacks, but each
read handle still needs explicit destruction.

Sessions expose status rather than readiness callbacks. The main-fiber pump
samples it, and idle requests a 1 ms sleep while any session is live. The pump
uses `cdrom_stream_session_try_destroy`: terminal status alone may precede
owner-request completion. The existing thread-blocking destructor is not used.
This costs periodic polling while a stream is open; it adds no worker thread.

## Boundaries

- Direct DMA uses the driver's current format/alignment/range rules. Ordinary
  RAM and permitted PVR RAM aliases go through the same submission entry point;
  GAPS submissions use the driver's separate lease-based entry point.
- GAPS leases remain caller-owned. Retain the lease and leave the destination
  untouched until await completes, even while queued or cancelling. The driver
  pins the lease only during execution; the adapter does not allocate or release
  it and does not change BBA ownership policy. After successful completion, a
  subsequent G2 transfer may consume the data. Failed reads may leave partial
  data: inspect status before consuming it.
- The driver manages cache visibility. The caller owns the destination and
  must not access, free, remap, or share it with another transfer before await
  completes. Dirty data sharing a destination cache line is not safe.
- A nonzero read timeout bounds execution, not initial queue residence. For
  an application deadline, the owner requests cancellation and continues
  draining; cancellation is not permission to reuse memory immediately.
- Submission/cancellation can allocate or take short kernel locks. This is
  cooperative waiting, not a claim of fully nonblocking real-time submission.
- One waiter per handle. Do not forcibly destroy a waiting fiber or terminate
  its owner thread. The adapter deliberately refuses destruction with live
  handles; it does not silently abandon DMA or callbacks during teardown.
- PIO reads, lease-based GAPS stream transfers, and other DMA engines are not
  wrapped. Do not substitute their thread-blocking waits.

See `examples/dreamcast/cdrom/fiber-read` for a live-media usage template and
`fiber-disc-contract` for a no-media lifetime/scheduling regression. Neither
example execution nor physical-drive timing is implied by a successful build.
