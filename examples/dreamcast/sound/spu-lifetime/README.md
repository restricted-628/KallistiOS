# SPU DMA ownership regression

This probe uses the real SPU request workers and callback dispatcher, with
link-time wrappers replacing only G2 submission/status/cancellation. The fake
engine refuses stop requests until the probe explicitly completes it. Both
caller cancellation and execution-deadline expiry must retain the request and
borrowed buffer until that completion; destroying an active request must fail.

Eight cases cover a wait timeout, refused cancellation, execution-deadline
expiry, software SUSPENDED state without retirement, queued cancellation,
callback reentry/input retention, a callback queued behind a parked callback,
and shutdown admission/engine/callback drain.
The callback case submits a follow-up request and checks that self-drain and
self-shutdown are refused. A parked callback keeps its input cookie live until
the dispatcher retires it. The shutdown case runs last and invokes the real
`spu_shutdown()`, disabling the SPU and clearing sound RAM on a target run.

The model's busy state and stop request are independent of the reported DMA
state. Completion is injected under IRQ exclusion from the test thread; this
does not exercise actual interrupt entry or establish register acknowledgement,
cache visibility, sample-fetch completion, or audible behavior.

Build with the normal KOS environment and run `spu-lifetime.elf`. Success prints
`SPU-LIFETIME: PASS cases=8 callbacks=9 submissions=10`. This is a scheduling/lifetime test,
not physical DMA cancellation or sound-RAM validation. For actual data movement
use `spu-transfer` and the generic `g2-state` example.

The host runner compiles this same main.c and the unchanged production
spu_request.c with pthread primitives and a worker model. It also extracts the
exact spu_shutdown body; hardware disable/clear are counted stubs. From the
repository root, run:

```sh
make -C examples/dreamcast/sound/spu-lifetime host-test
make -C utils/sound-stream-test test
```

Python 3 and a host C compiler with AddressSanitizer/UndefinedBehaviorSanitizer
are required. HOST_CC selects the compiler; HOST_CFLAGS overrides host flags.
SPU_HOST_BUILD_DIR and STREAM_LIFETIME_BUILD_DIR select output directories.
Default output directories are ignored. The stream suite tests exact extracted
stop/destroy bodies with deterministic dependencies, including an explicit
release-tail model gap. Its key-off/fetch-live state is a test input and supplies
no physical timing guarantee. Neither host runner uses sound hardware.
