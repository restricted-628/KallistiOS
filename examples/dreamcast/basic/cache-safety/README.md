# Cache maintenance safety probe

This regression verifies that data-cache maintenance and instruction-cache
synchronization accept a P2 uncached alias by operating on the equivalent P1
cache line. It also sends deliberately wrapping instruction- and data-cache
ranges through the public API; those invalid ranges must return without
iterating through wrapped addresses.

Success prints `KOSCACHE alias=1 overflow=1`.
