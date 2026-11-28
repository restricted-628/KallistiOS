# Integrated experimental fork: master

Use this branch for the combined Dreamcast kernel, drivers and optional addons.
It is not an official KallistiOS release. The following describes the tree
through `1c7114b374de` (2026-11-27); the [fork changelog](CHANGELOG.fork.md)
records subsequent changes. The [branch guide](BRANCHES.md) is a historical
map of separately scoped branches, not a list of prerequisites for master.

| If you need to… | Start here | Important limit |
| --- | --- | --- |
| Read immutable disc data on a page fault | [Demand paging](addons/libvmdemand/README.md) and [resident VM](doc/resident-vm.md) | Link the opt-in archive, admit the range on the reading carrier and retain the file. This is not demand paging for arbitrary pointers, writes or executable code. |
| Load sectors directly into device memory | [GD-DMA invariants](doc/direct-gdrom-dma-invariants.md) and [examples by destination](doc/direct-dma-examples.md) | VRAM and bridge SRAM have different ownership rules. The wave-RAM loader allocates its destination; it cannot overwrite an arbitrary playing buffer. |
| Mix ordered and autosorted tile passes | [PVR pass registration and list flush](doc/pvr-multipass-design.md) | A later clear redraws the background plane. Async flush borrows the source until its ticket retires; list completion is not render completion. |
| Choose a media component | [KOSdec](addons/libkosdec/README.md) | ADX checked playback, standalone mono AHX decoding, AFS archive access and the video-only SFD player are distinct interfaces. |
| Use the math or shader adapters | [SH4ZAM](addons/libsh4zam/README.md) and [SH4DER](addons/libsh4der/README.md) | Build against this checkout's pinned submodules. Do not mix installed headers or link two fiber providers. |

Start a new checkout with `--recurse-submodules`. After changing revisions,
run `git submodule update --init --recursive` and point `KOS_BASE` at that
checkout before rebuilding. The [fork overview](FORK.md) distinguishes upstream
facilities from fork defaults. Examples and hardware limits belong to their
component guides; a successful target link or emulator run is not a console test.
