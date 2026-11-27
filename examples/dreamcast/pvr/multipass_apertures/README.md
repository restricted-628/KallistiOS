# Five apertures in one tile-major render

Build with a configured Dreamcast KOS environment (`make`, then `make run`).
Use RGB565 at 640×480 and enable framebuffer emulation if testing in Flycast:
the example reads the actual displayed buffer, not a host screenshot.

The first pass draws four near white triangles around each diamond opening.
Five later passes draw colored rectangles behind the wall. Depth is preserved,
so each rectangle shows only through its opening. There is also a completely
empty middle pass. The final pass draws an independent red/green bar with
inside and outside user clipping using precisely the same tile coordinates.
No textures or external assets are needed.

| Pass | List | Contents |
| --- | --- | --- |
| 0 | opaque | near mask geometry for all five apertures |
| 1 | translucent, replacement blend | aperture 1 |
| 2 | opaque | aperture 2 |
| 3 | opaque | no application geometry |
| 4–6 | opaque | apertures 3–5; the clip probe is in pass 6 |

The guest runs the same two differently colored frames four ways:

1. All lists are RAM-buffered until scene finish (the image reference).
2. Pass 0 stays buffered while passes 1 and 2 submit directly to the TA
   using store queues. The first direct list is **not** in the first pass.
3. All lists have buffers, but pass 1 explicitly flushes its finished list
   before later geometry is submitted.
4. Pass 1 instead requests an asynchronous flush. The CPU prepares the next
   pane in disjoint memory while the finished list is borrowed. Continuing
   or finishing the scene must return `EBUSY` until the ticket is waited;
   waiting a second time must return `ENOENT`.

Each frame checks all seven region entries for each of the 300 tiles, including
their control flags and five list pointers. It verifies 18 independent expected
pixels and compares every pixel of the alternative paths with the corresponding
buffered frame. The public pipeline events report one final registration,
one render completion, and one display flip per ticket; the example checks the
render-start ticket separately. Seven region entries per tile do **not** mean
seven render starts or seven presented frames. This is a correctness test, not
a timing benchmark or an individual TA interrupt counter.

The final registration callback samples the TA parameter start, cursor and
limit before buffer rotation. The example checks the configured bank span
and reports parameter usage only when the cursor actually advances. If it
equals the start, the measurement is unavailable (as observed in Flycast).
Define `APERTURE_REQUIRE_TA_PROGRESS` when building for a target on which
nonzero cursor progress is required. The independent 3,744-byte application
packet count is the sum of generated headers, vertices and clip commands; it
does not include driver terminators and is not a proxy for TA parameter usage.

`APERTURE_PASS` is the success marker. A driver that still requires all
multipass DMA lists to have buffers is expected to reject the first mixed
translucent list on pass 1; that is a failure, not a skipped comparison.
