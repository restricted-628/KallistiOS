# Stereo PCM16 SQ regression

This standalone test overwrites two caller-selected scratch ranges in AICA
RAM near offsets 0x0fffe0 and 0x17ffe0 (plus 32-byte guards). Do not run it
alongside audio playback or another owner of those ranges. It uploads distinct
left/right sample sequences, reads both channels back, checks guards and
sample order, and verifies the outer recursive SQ mapping after each call.

Both MMU states are tested explicitly, including a 1 MiB boundary, reversed
channel-address order, direct P2 aliases, 16-byte channel tails, and batches
larger than 4 KiB. No Sega assets or sound data files are needed.

Success: `STEREO-SQ: PASS cases=32 mmu=2 order=1 tails=1 guards=1 restore=1`.
Emulator success does not establish physical FIFO timing, audio quality, or
performance. Test on hardware before claiming those properties. The separate
host model also covers 8 MiB AICA ranges, validation, and acquisition failures.
