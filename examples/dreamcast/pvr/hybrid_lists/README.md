# Buffered and direct PVR lists

This example leaves the opaque polygon list in direct store-queue mode while
assigning a RAM vertex buffer to the translucent polygon list. Each frame:

1. translucent geometry is collected in RAM;
2. `pvr_list_flush()` completes and transfers that list;
3. opaque geometry is submitted directly to the TA;
4. `pvr_scene_finish()` completes the scene without replaying the flushed list.

The expected image is a blue translucent triangle over a red opaque triangle.
The overlap should be purple. A duplicate translucent transfer usually appears
as excess blue or an otherwise unstable overlap, while a missing transfer
leaves only the red triangle.

The first frame also verifies that a second flush of the same list fails with
`EALREADY`. It also counts one successful PVR DMA completion event per frame
and rejects any latched DMA fault. The example aborts if a contract is
violated, otherwise renders 120 frames and prints
`RESULT: PASS (buffered/direct list submission)`.
