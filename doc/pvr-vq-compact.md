# Compact VQ codebooks

The PVR texture format reserves a 2,048-byte address window for 256 VQ
codebook entries before the index data. A texture does not need to store all
256 entries when its indices use only a smaller contiguous range at the high
end of that table.

KOS represents this layout with a normal `pvr_txr_surface_t` initialized by
`pvr_txr_surface_init_vq()`, `pvr_txr_surface_alloc_vq()`, or
`pvr_txr_surface_bind_vq()`. For `N` stored entries:

- the physical codebook size is `N * 8` bytes;
- the first legal index is `256 - N`;
- stored index data follows the compact codebook immediately; and
- the texture-header address is the storage address minus `2048 - N * 8`.

For example, a nonmipmapped 64x64 VQ texture with 16 entries stores 128 bytes
of codebook and 1,024 bytes of indices. Its encoded storage is therefore 1,152
bytes instead of the 3,072 bytes occupied by the full codebook. Its indices
must be 240 through 255, and its texture-header address is 1,920 bytes before
the stored codebook.

## Two addresses, two roles

`surface.vram` always identifies the first byte physically owned by the
surface: the stored compact codebook. Allocation, upload, readback,
reservation bounds, and release all use this storage address.

`pvr_txr_surface_get_texture_address()` returns the address encoded in a
polygon or sprite texture header. It is earlier for compact VQ because the PVR
still locates index data at a fixed 2,048-byte displacement. The omitted low
codebook slots may overlap preceding VRAM content because correctly biased
indices never select them.

The checked constructors reject a compact binding whose adjusted texture
address would fall outside PVR RAM. A caller packing textures manually should
therefore validate the complete binding rather than calculating the adjusted
address itself.

## Encoding and transfer

`pvr_txr_surface_get_vq_index_base()` reports the required index bias. Content
tools should add this value to every local codebook index and write only the
selected high-entry range. Runtime VQ encoding remains outside the kernel.

The stored representation is contiguous, so ordinary surface uploads and
readback work unchanged. A complete DMA upload additionally requires the
source, destination, and byte count to satisfy the existing 32-byte transfer
contract. Some codebook entry counts make the index-data boundary unsuitable
for an independent DMA range even though a complete transfer is aligned; CPU
range transfers remain available for those segments.

Compact surfaces can be placed in a contiguous texture reservation and used
through compact-model texture bindings. The reservation owns only stored
bytes, while material resolution automatically selects the adjusted sampling
address.

Compact codebooks are independent from the
[per-texture VQ palette layout](pvr-vq-palettes.md). The palette layout retains
all 256 entries and repurposes them as independent 16-bit colors; compact VQ
reduces the number of entries physically stored.
