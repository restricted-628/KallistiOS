# ISO9660 cache and lock regression

`make check` builds and runs the **current production** `fs_iso9660.c` inside
one host translation unit. It requires a GNU-compatible C compiler with
`-finstrument-functions`, pthreads, and Python 3. The default flags use GNU17.
Clang's emulated GCC version triggers a KOS header warning, so this host-only
build suppresses `-Wcpp`; other warnings are errors.

The fixture is a synthetic two-directory ISO image. Function instrumentation
pauses the real reader immediately after `biread()` returns and before parsing.
A competing real ISO operation then either completes or demonstrably waits for
the foreground lock. A three-second lock/rendezvous bound turns deadlocks into
failures; the runner also bounds each process to ten seconds.

Coverage includes interleaved open/stat/readdir, reset and media invalidation,
BIOS stream/cache ordering, ordinary partial reads, mount and precise errors,
deferred remount, fast request and stream publication, pending close, callback
reentry, cancellation retirement, failed submission unwind, and stale directory
prefetch rejection. Direct-only cases require async admission to complete while
another foreground operation holds the cache lock, and require same-descriptor
admission during a synchronous read to return `EBUSY`. Retirement must also
finish under a competing cache hold, including release of the last retained
handle. BIOS legacy stream seek/close and streamed reads exercise blocking
transport spies that reject holding the finalizer's descriptor lock.

Transport, request scheduling, and VFS references are deliberately modeled.
The driver code, admission, parsing, publication, finalizers, callbacks, and
close paths execute unchanged. Synthetic async reads complete one segment;
this is not a validation of request-engine scheduling, DMA, cache coherency,
firmware, cancellation quiescence, or physical media transitions. Fixtures are
process-local and some persist to process exit; leak detection is not a test
of driver shutdown here.

To compare another revision, extract its driver to a temporary file outside
the checkout, then force a rebuild (changing a make variable is not itself a
dependency):

```sh
make -B ISO_SOURCE=/absolute/path/to/fs_iso9660.c
python3 run.py ./iso9660-lock-test /absolute/path/to/results.json
make -B check
```

The alternate source must have compatible fork interfaces. The include search
path resolves its private hardware headers from this checkout. Do not replace
this fixture with the copied reader from `utils/isotest`.
