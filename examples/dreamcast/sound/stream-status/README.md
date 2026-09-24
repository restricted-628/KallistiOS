# Checked stream lifecycle

This example generates stereo PCM in main RAM and plays it through the checked
stream API. It deliberately supplies one short block and one empty block so the
stream manager must silence-pad both without reading beyond the callback data.

The program verifies live control updates, coherent progress, underrun
accounting, bounded stop, and checked destruction. It needs no romdisk or
external audio asset.
