# Flash configuration and play-history reader

This example reads the validated extended system configuration and attempts to
decode play-history slot zero. It never writes to or erases flash.

The slot can legitimately be empty. In that case the example prints the
negative block-store result and still exits successfully after demonstrating
the configuration path.

Build with `make` and run with the configured KOS loader using `make run`.
