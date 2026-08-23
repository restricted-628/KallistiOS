# Scanout and display-filter state

This example validates the coherent physical scanout snapshot, checked
framebuffer output-filter controls, and opt-in raster callbacks. It observes a
changing physical scanline, checks invalid-input errors, toggles the
display-owned filter, confirms that full-scene antialiasing remains owned by
PVR initialization, and verifies that removing the last raster handler stops
callback delivery.

The original filter state is restored before the example exits. The reported
scanline is a hardware timing counter rather than a framebuffer Y coordinate.

Build with `make`, or use `make run` with the configured KOS loader.
