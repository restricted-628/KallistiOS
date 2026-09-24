# Compact-codebook VQ

This example stores a 64x64 VQ texture with only 16 codebook entries. The
encoded texture occupies 1,152 bytes: 128 bytes of codebook followed by 1,024
bytes of VQ indices. The equivalent full-codebook texture occupies 3,072
bytes.

The example queries the legal index base, writes indices 240 through 255, and
uses `pvr_txr_surface_get_texture_address()` when compiling the polygon header.
Uploads and release continue to use the surface's unadjusted storage address.
It renders the texture for 120 frames and verifies that the PVR pipeline
reports no persistent fault.

Compact VQ gains its storage reduction by leaving unused low-numbered codebook
entries physically absent. The texture sampling address is consequently
earlier than the stored codebook, while the biased index values ensure that
only the present entries are selected.
