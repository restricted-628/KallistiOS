# Complete SH4ZAM fiber + Service Executor SDK branch

This is the user-facing `addon/fiber-service-sh4zam` integration branch. One checkout contains
the runtime, OS prerequisites, direct disc/G1/G2/GAPS support, `libfiber_disc`,
and RAM/VRAM examples. Do not assemble it from other branches or link against
another checkout's archives. This is a complete SDK based on integrated master
`80be9175`, not a minimal upstream fiber patch; existing SDK graphics and
other integrated features remain present.

This branch builds only the addon fiber implementation. Kernel fiber runtime
sources and Service Executor implementation are removed from the kernel;
`libfiber_sh4zam` owns the runtime, synchronization, context switch and executor.
Its accelerator operations use SH4ZAM directly, without KOS math fallbacks.
The standard KOS link group selects this provider automatically. Public fiber
and service symbols are not advertised as kernel dynamic exports.

## Get and build one checkout

```sh
git clone --branch addon/fiber-service-sh4zam --single-branch --recurse-submodules \
  https://github.com/restricted-628/KallistiOS.git kos-fibers
cd kos-fibers
```

The pinned SH4ZAM submodule preserves its upstream history. If cloned without
submodules, run `git submodule update --init --recursive`. No separate SH4ZAM
source checkout or manual branch merge is needed.

Configure the normal KOS toolchain environment for **this** directory
(`KOS_BASE` must point here), following [KOS setup](doc/README.md).
Then, from the checkout root:

```sh
make -j4
bash utils/check-fiber-bundle.sh
```

The check builds runtime probes and the fiber-disc contract/RAM/VRAM examples,
audits provider symbols and link maps, and rejects a different `KOS_BASE`.
It does not run an emulator or prove physical hardware behavior. Use a fresh
checkout when changing bundles; if reusing a directory, clean the previous
build before switching and rebuild everything.

Applications use `kos/fiber.h` and `kos/fiber_sync.h`. Link `-lfiber_disc`
for cooperative direct-disc waiting. Include `kos/fiber_service.h` explicitly
when using the Service Executor; the executor is not required by disc waits.
The adapter parks application fibers while the disc worker owns hardware.
It does not run disc I/O on the Service Executor.

## Included examples and limits

- `examples/dreamcast/basic/threading/fiber*`: selected-provider tests.
- `examples/dreamcast/cdrom/fiber-disc-contract`: real queue/fibers with mocked transport.
- `examples/dreamcast/cdrom/fiber-read`: direct DMA to application RAM.
- `examples/dreamcast/cdrom/fiber-vram`: generated texture, direct DMA to VRAM,
  readback, guards and render-lifetime fencing.
- `examples/dreamcast/cdrom/direct-gaps-stage`: serialized G1-to-SRAM then G2
  transfer, with explicit owner/lease lifetimes.

GAPS belongs to the shared direct-access series; it is not a fiber provider.
The first fiber-disc adapter does not yet wrap GAPS leases or staged streams.
Resident-loader detach/hot switching and physical BBA validation are still
open. See [GAPS ownership](doc/gaps-ownership.md) and the example READMEs.
No proprietary middleware, BIOS, media or reverse-engineering data is added.

See [validation results and limits](doc/fiber-bundle-validation.md). Changes
to the provider, disc adapter or driver must be retested in both bundles.
