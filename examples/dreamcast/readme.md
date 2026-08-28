# KallistiOS Examples
This page serves as an index for all KallistiOS examples.

- [**2ndmix**](2ndmix/): The flagship KallistiOS demo. It is a remixed version of _Stars_, the first publicly available homebrew Dreamcast demo!
- basic
  - asic-event-claim
  - asserthnd
  - breaking
  - cache-safety
  - dma
    - g2-state
    - speedtest
  - exec
  - fpu
  - gdb_breaking
  - independent-heap
  - [**animation-playback**](basic/animation-playback/): Demonstrates opt-in,
    caller-owned clip playback and object/camera/light binding
  - [**camera-matrices**](basic/camera-matrices/): Demonstrates checked,
    caller-owned perspective and look-at matrix construction
  - [**collision**](basic/collision/): Demonstrates checked, allocation-free
    ray, triangle, sphere, capsule, segment, plane, AABB, and OBB geometry
  - [**matrix_stack**](basic/matrix_stack/): Demonstrates bounded caller-owned
    transform hierarchy state
  - memtest32
  - mmu
    - mapping-safety
  - posix_resource
  - stackprotector
  - stacktrace
  - sq-safety
  - threading
    - atomics
    - barrier
    - compiler_tls
    - general
    - once
    - recursive_lock
    - reentrant_mutex
    - rwsem
    - spinlock_test
    - tls
    - vblank-priority
    - workqueue-safety
  - watchdog
- cdrom
  - cdda-status
  - direct-async
  - direct-cdda
  - direct-geometry
  - direct-gaps-stage
  - direct-lifecycle
  - direct-read
  - direct-recovery
  - direct-status
  - direct-iso9660
  - media-recognition
  - request
  - sector-range
  - stream
- conio
  - adventure
  - basic
  - conio_dbgio
  - kosh
  - wump
- cpp
  - clock
  - concurrency
  - dcplib
  - filesystem
  - gltest
  - modplug_test
  - out_of_memory
- dev
  - devroot
  - [**random**](dev/random/): Demonstrates generating random numbers using /dev/urandom
- dreameye
  - basic
  - sd
- filesystem
  - browse
  - iso9660-async
  - pty
  - sd
- g1ata
  - atatest
- gldc
  - 2D_tex_quad
  - basic
  - benchmarks
  - nehe
- [**hello**](hello/): demonstrates printing text to the console
- keyboard
  - keyrawtest
  - keytest
- libdream
  - 320x240
  - 640x480
  - cdfs
  - keyboard
  - lcd
  - mouse
  - rgb888
  - spu
  - ta
  - vmu
- library
- lightgun
  - basic
- lua
  - basic
- maple
  - [**controller-snapshot**](maple/controller-snapshot/): Validates coherent controller samples, transitions, capability decoding, and callbacks
  - [**keyboard-snapshot**](maple/keyboard-snapshot/): Validates keyboard metadata, key state, and coherent snapshots
  - [**lightgun-capture**](maple/lightgun-capture/): Demonstrates scheduled light-gun capture and coherent aim snapshots
  - [**mouse-snapshot**](maple/mouse-snapshot/): Validates full mouse conditions, metadata, transitions, and coherent snapshots
- [**micropython**](micropython/): Demonstrates basic usage of the MicroPython kos-port
- modem
  - basic
  - ppp
- mruby
  - dreampresent
  - mrbtris
- network
  - basic
  - dns-client
  - httpd
  - isp-settings
  - ntp
  - ping
  - ping6
  - speedtest
  - udpecho6
- objc
  - runtime
- parallax
  - bubbles
  - delay_cube
  - font
  - raster_melt
  - rotocube
  - serpent_dma
  - sinus
- [**png**](png/): - Demonstrates the use of PNG textures, gzip decompression, and drawing text
- profiling
  - gcov
  - gprof
- pthread
  - general
- pvr
  - background_plane
  - bumpmap
  - cheap_shadow
  - [**chunk_asset**](pvr/chunk_asset/): Generates a versioned compact-model
    asset, LZ4-compresses its vertex partition, and loads it through exact
    caller-owned workspace without starting a service thread
  - [**chunk_asset_disc**](pvr/chunk_asset_disc/): Reads a compact-model asset
    through direct GD-DMA and optionally a leased GAPS/G2 staging path, then
    verifies LZ4 decoding and reports separate transport and decode timings
  - [**chunk_resources**](pvr/chunk_resources/): Converts and embeds a compact
    model, then resolves its texture identifiers through a pre-acquired
    fixed-slot residency set, checked materials, and an admitted signed-light
    diffuse-plus-specular render policy
  - [**chunk_skin**](pvr/chunk_skin/): Binds explicit normalized joint
    influences, builds a reusable canonical source, and renders a moving
    deformed pose through constant-time model-index lookup
  - clipping
  - fb_tex
  - [**geometry_contract**](pvr/geometry_contract/): Projects caller-owned
    geometry with bounded frustum clipping, checked material compilation, and
    established PVR list sinks
  - hybrid_lists
  - material_state
  - multipass
  - multipass_dma
  - multipass_hybrid
  - modifier_volume
  - modifier_volume_tex
  - modifier_volume_zclip
  - palette
  - pipeline_status
  - [**particles**](pvr/particles/): Simulates a caller-owned particle pool and
    emits hardware sprite cells plus colored polygon trails
  - plasma
  - pvrline
  - pvrmark
  - pvrmark_strips
  - pvrmark_strips_direct
  - render_ticket
  - [**sprite_cells**](pvr/sprite_cells/): Compiles reusable atlas cells and
    caller-owned instances into existing textured PVR sprite packets
  - strided_texture
  - [**texture_reservation**](pvr/texture_reservation/): Packs multiple checked
    surfaces into one caller-owned contiguous VRAM reservation
  - [**texture_residency**](pvr/texture_residency/): Cycles three textures
    through two pinned, generation-checked LRU slots using asynchronous DMA
  - texture_surface
  - texture_render
  - [**vq_palette**](pvr/vq_palette/): Uses a VQ codebook as an independent
    per-texture 16-bit palette for a byte-indexed image
  - [**vq_compact**](pvr/vq_compact/): Stores only the high VQ codebook entries
    used by a texture and compiles its adjusted sampling address
  - yuv_converter
- raylib
  - raytris
- [**rumble**](rumble/): Validates typed vibration effects, device metadata, auto-stop, and asynchronous completion
- sd
  - ext2fs
  - mke2fs
- sh4zam
  - bruces_balls
  - [**integration**](sh4zam/integration/): Verifies the bundled SH4ZAM 0.8
    library, default link integration, alias-safe KOS math bridges, and bounded
    compact-model emission
- sdl
  - sound
- sound
  - cdda
  - [**channel-sync**](sound/channel-sync/): Starts AICA channels 31 and 32
    together with one synchronized 64-channel key-on
  - ghettoplay-vorbis
  - hello-adx
  - hello-mp3
  - hello-ogg
  - hello-opus
  - multi-stream
  - sfx
  - sfxbuf
  - [**spu-transfer**](sound/spu-transfer/): Validates queued DMA and exact-byte
    PIO sound-RAM upload and readback requests
- tsunami
  - banner
  - font
  - genmenu
- video
  - bfont
  - bfont-glyph-query
  - [**framebuffer-query**](video/framebuffer-query/): Validates displayed, drawing, and indexed framebuffer surfaces
  - minifont
  - [**mode-policy**](video/mode-policy/): Resolves cable-aware 50/60 Hz video modes without installing them
  - multibuffer
  - palmenu
  - [**scanout-filter**](video/scanout-filter/): Validates physical scanout, display filters, and opt-in raster callbacks
  - screenshot
- vmu
  - vmu_beep
  - [**vmu_clock**](vmu/vmu_clock/): Validates civil-time rules and coherent
    synchronous and asynchronous clock reads
  - vmu_game
  - [**vmu_lcd**](vmu/vmu_lcd/): Validates LCD descriptors, raw bitmap
    ordering, orientation, and asynchronous completion
  - vmu_pkg
