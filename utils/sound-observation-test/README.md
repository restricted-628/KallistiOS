# Sound command observation tests

Run `make test`, or `make clean test EXTRA_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`.

`arm.c` includes the production ARM dispatcher and links the production observer.
It calls the same copied-packet dispatch function as the queue parser. Hardware
helpers are fakes that assert valid channel indices. The queue's fixed-address
memory copy is not executed on the host; pointer-width cast warnings are disabled
only for that legacy ARM source. The fixture supplies fixed-width wire aliases. Compiler function-entry hooks count
production validator invocations without adding instrumentation to production
source. Hardware-helper counters check periodic sampling and legacy invalidation.

`host.c` includes the production SH-4 observation implementation and links the
ARM observer. `extract.py` extracts exact bridge/validation functions from
`snd_iface.c` and prints their hashes. Pthread-backed KOS lock stubs exercise
recursive queue locking, concurrent lazy binding and finite deadlines. A fake
FIFO connects both implementations, with delays, refusal and paused processing.
G2 reads use the production coherent-read body over fake RAM. These tests prove
software transitions only, not G2 arbitration, ARM memory ordering, hardware
sample fetch completion or audio output.

`registers.c` executes the production ARM channel-start and monitor helpers with
injected MMIO storage. It checks that eligible starts drain the selected channel
after key-off and before key-on, preserve another channel's pending LP input,
and exclude delayed/looped/unsupported starts. Read-clear behavior is an injected
input; these checks do not establish physical selector or LP attribution timing.

The target `channel-observation` example covers STOP receipt processing, fresh
challenges, cooperative legacy invalidation and explicit result consumption.
It allocates no sample data. Old firmware prints an unsupported capability
marker; that path does not count as successful observation execution.
