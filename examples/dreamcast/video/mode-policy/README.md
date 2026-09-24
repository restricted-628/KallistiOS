# Cable-aware video mode policy

This read-only example reports the connected display cable and active KOS video
mode. It reads the system region, chooses an explicit 50 or 60 Hz preference,
and asks `vid_mode_resolve()` what `DM_640x480` would select.

The resolved candidate is printed but never installed. This makes the example
safe to run on displays whose accepted timings are unknown. An application can
use the same policy with `vid_set_mode_standard_checked()` after presenting any
required regional or 60 Hz confirmation UI.

Build with `make`, or use `make run` with the configured KOS loader.
