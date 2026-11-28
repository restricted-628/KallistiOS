# KallistiOS Addons

The Addons system allows the creation of standalone addon packages, similar to the ports in the kos-ports system. 

## Installing addons
The aggregate [Makefile](Makefile) discovers addons with a matching
`kos/$(KOS_ARCH).cnf`. A directory under `addons/` alone does **not** opt it
into the build. For an explicitly built archive, follow its own build, install
and link instructions; do not assume it is part of the kernel or was installed
by building KOS.

A few addons are supplied with KallistiOS. These include:

- [libvmdemand](libvmdemand/README.md): opt-in read faults on admitted immutable disc-file ranges.
- [KOSdec](libkosdec/README.md): independently buildable codec, player and archive components.
- [libsh4der](libsh4der/README.md): effects and math adapters for the existing renderer.
- [liblz4](liblz4/README.md): bounded decoding and cooperative service adapters.
- [**libkosext2fs**](libkosext2fs/): A filesystem driver for the ext2 filesystem
- [**libkosfat**](libkosfat/): A filesystem driver for FAT12, FAT16, and FAT32 filesystems, with long name support
- [**libkosutils**](libkosutils/): Utilities: Functions for B-spline curve generation, MD5 checksum handling, image handling, network configuration management, and PCX images
- [**libnavi**](libnavi/): A flashROM driver and G2 ATA driver, historically used with Megan Potter's Navi Dreamcast hacking project
- [**libppp**](libppp/): Point-to-Point Protocol support for modem devices
- [**libsh4zam**](libsh4zam/): SH-4-optimized scalar, vector, matrix,
  quaternion, and XMTRX math routines
- [**libfiber_disc**](libfiber_disc/): Optional cooperative direct-disc DMA
  waits for application-owned fibers, without a service executor

## Creating addons
Although KallistiOS currently only supports the Dreamcast platform, the system is designed to support build quirks for various platforms. Each addon contains a `kos` directory, which would contain `$(KOS_ARCH).cnf` files (so, as of now, just `dreamcast.cnf`). The addon's `Makefile` contains build instructions to build the addon for KallistiOS, while the `dreamcast.cnf` contains quirks specific to building for the Dreamcast. If there are no platform-specific build quirks, an empty file named `dreamcast.cnf` still needs to exist for the build system to recognize that Dreamcast is a valid target platform for the addon.
