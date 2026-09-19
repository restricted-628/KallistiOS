# Fixed-slot texture residency

This example cycles three 64x64 textures through two identically sized VRAM
slots. A cache miss reserves either an empty slot or the least-recently-used
unpinned slot, uploads the encoded texture through the existing asynchronous
PVR DMA request API, and publishes it only after successful completion.

Every acquired or newly published slot remains pinned until rendering is
complete. The final access pattern revisits the first texture before loading
the third, demonstrating that the second texture is selected for eviction.
The example validates the resulting hit, miss, eviction, and pin counts after
120 frames.

The residency layer owns one contiguous VRAM reservation. Its slot and surface
metadata arrays remain caller-owned, and it creates no main-RAM allocation,
worker thread, transfer queue, decompressor, or periodic activity.
