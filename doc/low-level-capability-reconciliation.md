# Low-level capability reconciliation

## Scope

This document reconciles the useful low-level capabilities of the historical
Dreamcast development environment with KallistiOS facilities. It tracks
behavior, not source vocabulary, work-area conventions, or compatibility
symbols. New facilities belong to existing KOS subsystems and retain KOS
lifecycle, error, threading, and ownership rules.

The cumulative review branch currently contains the dependency chain through
VMU storage closure. Later rows marked **integration checkpoint** already have
software implementations in the protected integration tree, but still need to
be reconstructed as reviewable commits on top of that chain.

## Capability matrix

| Capability | KOS owner | Current state | Remaining gate |
|---|---|---|---|
| Disc command transport | G1 arbitration, BIOS backend, direct GD transport | Cumulative branch complete | Physical drive and pressed-media timing |
| Synchronous and asynchronous disc reads | CD request engine and direct transport | Cumulative branch complete | Physical cancellation and cache-alias testing |
| Sector ranges, staged reads, seek, status, subcode, and CD audio | CD request/range services | Cumulative branch complete | Physical media and multi-track audio checks |
| ISO9660 byte and sector I/O | ISO9660 VFS backend, async planner, snapshots | Cumulative branch complete | Physical media-change testing |
| Controller, keyboard, mouse, light gun, and vibration | Maple device drivers | Cumulative branch complete | Device variants and physical orientation/timing |
| VMU display and clock | Maple VMU driver | Cumulative branch complete | Physical display orientation and clock writes |
| VMU metadata and package validation | VMU filesystem and package utilities | Cumulative branch complete | Physical flash behavior |
| VMU transactional save, delete, rename, attributes, format, and defragment | VMU filesystem transaction/request layers | Cumulative branch complete | Removal and power-loss behavior on hardware |
| VMU partial block rewrite | VMU filesystem copy-on-write rewrite service | Cumulative branch complete | Executable-file interruption behavior on hardware |
| VMU orphan reclamation | VMU whole-filesystem validator and repair service | Cumulative branch complete | Physical flash interruption behavior |
| Multi-bank memory-card control | Maple memory-card control plus lazy VMU requests | Cumulative branch complete | Compatible physical multi-bank devices |
| Timers and callback scheduling | TMU, VBlank, work queues, timer events | Cumulative branch complete | Physical timing sweep |
| Fibers and service executors | Optional per-thread fibers and explicit carrier services | Cumulative branch complete | Stress testing with MMU on/off |
| MMU, cache, store queues, and independent heaps | SH-4 memory services and heap API | Cumulative branch complete | Physical cache/MMU testing |
| Flash configuration, play history, and RTC | Flash layout transactions and RTC services | Cumulative branch complete | Physical write timing |
| Video/cable policy | Video mode validation and snapshots | Cumulative branch bounded base complete | Additional timing modes remain separate |
| Serial configuration and status | SCIF driver | Cumulative branch bounded base complete | DMA and raw modem pins remain separate |
| Expansion and network-device discovery | Expansion probe and existing network drivers | Cumulative branch complete | Physical device variants |
| External-bus and interrupt ownership | G1, G2 DMA, and ASIC ownership layers | Cumulative branch complete | Physical contention and timing sweep |
| Microphone input | Maple microphone transport and caller-owned capture rings | Integration checkpoint base complete | Extraction and physical devices |
| Camera stored-image transport | Maple camera driver | Integration checkpoint complete | Extraction and physical devices; live video is separate |
| Sound output robustness | AICA queue, allocator, stream, effect, and SPU services | Integration checkpoint base complete | Extraction; wider synchronized start needs firmware coordination |
| Runtime and filesystem lifetime | Name manager, descriptors, mounts, ordered shutdown | Cumulative branch complete | Broader application stress testing |

## Separate projects

Graphics scene management, windowing, codecs, sound-content sequencing, and
content middleware are application-facing libraries rather than unfinished
kernel drivers. They should use the completed low-level services, but should
not be folded into this closure series or initialized for applications that do
not use them.

## Completion rule

A software row closes only when its public declarations, implementation,
exports, documentation, host tests where practical, cross-build, and examples
agree. Emulator execution is integration evidence. Electrical behavior,
removable-media timing, flash interruption, and third-party device behavior
remain explicit physical-hardware gates.
