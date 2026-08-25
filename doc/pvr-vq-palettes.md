# Per-texture palettes through VQ

The PVR's VQ codebook can represent a private 256-color palette for one
texture. Each VQ entry contains four 16-bit texels. Repeating one color across
all four positions makes the entry behave as a single indexed color when the
hardware texture is declared at twice the logical width and height.

For a logical `W` by `H` byte index image:

- initialize an ordinary VQ surface at `2W` by `2H`;
- build its full 2048-byte codebook with `pvr_txr_vq_palette_build()`;
- store the `W * H` indices in the ordinary twiddled VQ index order; and
- compile the texture header with the surface's doubled dimensions.

A logical 256x256 texture therefore uses a 512x512 VQ surface containing a
2048-byte private palette and 65536 index bytes. The equivalent ordinary
16-bit 256x256 texture occupies 131072 bytes. Global palette-bank state is not
used, although each private color has the selected surface format's 16-bit
precision rather than the wider global palette modes.

This layout uses all 256 hardware codebook entries. It is separate from compact
codebook storage, which changes the physical codebook size and requires biased
VQ indices and a correspondingly adjusted texture address. See the
[compact VQ codebook guide](pvr-vq-compact.md) for that layout.
