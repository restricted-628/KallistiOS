# Expansion capability probe

This example prints the expansion-port device class, capabilities, interrupt
ownership, and probe completeness without initializing a network stack.

The default invocation is side-effect-free. It can identify the PCI Ethernet
adapter and observe an already-active 8-bit device, but it deliberately does
not reset an inactive 8-bit interface.

Pass `--reset-8bit` to permit the bounded LAN-adapter and modem tests. Those
tests reset the inactive 8-bit interface and can take several hundred
milliseconds. The API refuses this mode while an 8-bit driver owns the
external interrupt.
