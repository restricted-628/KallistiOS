# BIOS command ownership regression

This links the production CD-ROM command code but replaces G1 acquisition,
polling, and the relevant firmware entry points with link-time spies. Normal
disc initialization is disabled. No drive command or real DMA is issued.

Four cases cover successful completion, timeout followed by successful abort,
timeout followed by abort timeout/reset, and failed bus acquisition. Every
firmware call must occur while the same claim is held; no release is allowed
between command timeout and abort/reset. A completed cleanup must release once
and leave no active command. Success prints `G1-COMMAND: PASS cases=4`.

This tests control flow, not firmware timing, drive recovery, or optical/ATA
hardware interoperability.
