# Per-texture VQ palette

This example uses a full VQ codebook as a private 256-color, 16-bit palette.
Each codebook entry repeats one color across its complete 2x2 texel block. A
64x64 byte index image is therefore stored as the VQ data of a 128x128 hardware
texture and rendered as 64x64 independently selected colors.

The example builds the codebook with `pvr_txr_vq_palette_build()`, twiddles the
byte index plane, uploads one checked VQ surface, renders it for 120 frames, and
verifies that the PVR pipeline reports no persistent fault.

This is distinct from reducing the number of physically stored VQ codebook
entries. It retains all 256 entries but gives each texture its own colors
without consuming a global palette bank.
