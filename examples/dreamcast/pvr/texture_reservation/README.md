# Contiguous texture reservations

This example initializes three checked texture surfaces, plans aligned and
non-overlapping offsets, and obtains one contiguous allocation from KOS's
established PVR memory allocator. Each surface borrows an exact slice of that
reservation and is uploaded and rendered normally.

The example proves that the surfaces share one allocation while retaining
independent metadata and transfer bounds. It renders for 120 frames, checks the
persistent PVR fault record, clears every borrowed surface, releases the single
reservation, and displays a green pass marker.
