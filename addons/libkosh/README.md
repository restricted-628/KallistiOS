# Kosh addon

Editable in-tree source from [KallistiOS/libkosh](https://github.com/KallistiOS/libkosh),
commit `535a1f04c141aa61ae176dc2d11c373ab8b6c493`. Upstream history remains
available in that repository; this is a source snapshot, not a history merge.
Original author notices are retained. License: [KOS License](../../doc/license/LICENSE.KOS),
as identified by the KallistiOS kos-ports libkosh recipe.

Kosh depends on the fork's bundled Conio addon. This baseline supports future
Nindows2-parity work; it does not yet add Nindows2 features. Runtime sources and
headers are initially unchanged. The local Makefile uses the in-tree addon
build, configured KOS C standard and dependency files without rewriting any
kos-ports headers. Public headers use a relative in-tree link.

After loading this checkout's KOS environment:

```sh
make -C addons/libconio
make -C addons/libkosh
make -C examples/dreamcast/conio/kosh
```

Include `<kosh/kosh.h>` and `<conio/conio.h>`, then link `-lkosh -lconio`.
Archives live in `addons/lib/dreamcast`; the standard KOS search order selects
them before kos-ports. Neither library is added to the default kernel link group
or automatically started. Applications still own initialization and shutdown.

The owner intends substantial Kosh/Conio evolution within this fork. Preserve
this import's provenance and review behavior/API changes separately; successful
compilation alone does not certify the inherited shell or lifecycle behavior.
