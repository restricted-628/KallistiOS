# Cache maintenance safety probe

This regression verifies that data-cache maintenance and instruction-cache
synchronization accept a P2 uncached alias by operating on the equivalent P1
cache line. It also sends deliberately wrapping instruction- and data-cache
ranges through the public API; those invalid ranges must return without
iterating through wrapped addresses.

Cross-area requests are also tested. Success prints
`KOSCACHE alias=1 overflow=1 area=1`.

An emulator which bypasses cache modeling can pass without exercising actual
write-back or tag behavior. This is a runtime smoke check, not hardware proof;
repeat on a physical console before claiming cache-coherency validation.
