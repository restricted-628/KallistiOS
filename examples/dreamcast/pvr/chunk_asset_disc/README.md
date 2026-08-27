# Direct-disc compact-model asset benchmark

This example compares the two native input paths for a compressed compact-
model asset:
direct GD-DMA into system RAM, and direct GD-DMA into a caller-owned GAPS
SRAM lease followed by timed G2-DMA into system RAM. It performs no BIOS
filesystem payload reads and does not allocate a worker or fiber. Each path
then performs LZ4 Frame decoding, section CRC verification, and complete model
admission. Decode time is reported separately from transport time.

The build creates `chunk-asset-model.pcm`; place that file at the root of the
test ISO as `/chunk-asset-model.pcm`. The normal ISO9660 path is used only to
discover its FAD and exact recorded size. Defining both `CHUNK_ASSET_FAD` and
`CHUNK_ASSET_BYTES` bypasses even that metadata lookup. The GAPS comparison is
skipped when the bridge is absent or its SRAM is already leased by another
driver; the direct-RAM pipeline must still pass.
