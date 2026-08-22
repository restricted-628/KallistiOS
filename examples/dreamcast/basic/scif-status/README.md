# SCIF status example

This observational example prints the current byte-oriented serial
configuration, actual generated bitrate, FIFO occupancy, IRQ mode, and
cumulative error counters through scif_get_status().

It does not reconfigure the port, enable receive interrupts, consume received
bytes, or send application data directly. Normal console output still follows
the debug-I/O handler selected by the running program.

Build with make, or use make run with the configured KOS loader.
