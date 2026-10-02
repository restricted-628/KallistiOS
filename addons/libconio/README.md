# Conio addon

Editable in-tree source from [KallistiOS/libconio](https://github.com/KallistiOS/libconio),
commit `36c234b93c6ba44b58417c18b45828ec62b4d1c2`. Upstream history remains
available in that repository; this is a source snapshot, not a history merge.
Original author notices are retained. License: [KOS License](../../doc/license/LICENSE.KOS),
as identified by the KallistiOS kos-ports libconio recipe.

This is the baseline for the fork's Kosh-based Nindows2-parity addon work, not
an implementation of Nindows2. Runtime sources and headers are initially
unchanged. Local changes adapt the build to this tree: no writes into kos-ports,
relative public-header routing, dependency files, the configured KOS C standard,
and additive Dreamcast graphics flags rather than replacing caller CFLAGS.

After loading this checkout's KOS environment:

```sh
make -C addons/libconio
```

Headers are exposed as `<conio/conio.h>`, `<conio/draw.h>` and `<conio/input.h>`
through `addons/include/conio`. Link `-lconio`; the archive is built into
`addons/lib/dreamcast`. This uses the normal addon build and is not automatically
linked or initialized by the kernel. The fork's addon include/library paths
precede kos-ports; do not override them with another Conio installation.

Existing consumers include `examples/dreamcast/conio/basic` and `conio_dbgio`.
Build/link success is not proof of input, drawing or shutdown behavior on hardware.
Keep functional changes separate from this baseline import and record upstream
comparison revisions when updating the source.
