# Checked network retirement regression

Run `gmake -C utils/net-retirement-test test` from the repository root. The host
suite uses Clang, GNU17, AddressSanitizer and UndefinedBehaviorSanitizer by
default. `extract.py` compiles selected production function bodies verbatim with
controlled boundary shims. Generated includes and test executables are ignored.

The driver cases cover admission closure, DMA generation/status/callback refusal,
late callback tails, complete polled-operation admission, borrowed input,
link-change IRQ closure/startup, worker join ownership, partial
initialization, repeated registration/shutdown and retained hardware. Network
cases cover queue join proof, initialization exclusion, retry, early self/context
refusal and consumer preservation. Platform cases execute the actual caller
barriers and terminal handler with a bounded sleep seam, including the actual
SCIF byte-access predicate and nonblocking writer with controlled registers.
`callers.py` additionally checks the architecture-only exec ordering.

The archive fixture checks optional and manually initialized networking, plus
paired and unpaired legacy overrides. On macOS, the archive cases are explicitly skipped because Mach-O optional
data symbols and archive extraction differ from SH ELF. The actual-body host
dispatch tests still run. After
sourcing the configured KOS environment, run all five target linkage cases:

```sh
python3 utils/net-retirement-test/linkage.py --sh --cc "$KOS_CC" \
    --ar "$KOS_AR" --nm "$KOS_CC_BASE/bin/sh-elf-nm"
```

Build the resident scheduler probe after building the kernel:

```sh
kos-cc utils/net-retirement-test/target.c -o /tmp/net-retirement.elf
```

Run that ELF in the normal Dreamcast emulator setup. It prints
`NETRET RESULT PASS` only when the actual terminal handler has published the
original reason and another thread continues to make progress. The probe stays
resident; the harness/operator must stop the emulator after the complete result
line. It does not exercise NIC shutdown or loader return.

Host shims establish software ordering under controlled outcomes. Target linkage
establishes SH ELF symbol behavior. Scheduler execution is separate from physical
NIC, G2, cache, interrupt timing and power evidence. Acquired BBA hardware remains
retained: software quiescence does not authorize SRAM/owner release or restoration
of a resident loader. Legacy exit/exec can therefore wait indefinitely, as selected
by the terminal policy. Early invalid/self-stop refusals can precede admission
closure and preserve that earlier state.

Custom init-flag pointer hooks use the private `net_shutdown_binding` declarations
in `kernel/net/net_shutdown_internal.h`. Define `arch_net_shutdown_custom` and/or
`arch_eth_shutdown_custom` with the exact selected legacy function pointer and
its typed `int check(void)` partner. An absent, mismatched or empty binding
refuses with ENOSYS even when stock checked providers are linked. Custom checks
run before stock checks and their failures propagate. Stock manually initialized
net/BBA providers are always checked independently. This does not make direct
replacement of strong public functions a supported link surface.

The real-library pointer probe builds five variants (0: off, 1: INIT_NET on,
2: manual net_init invocation, 3: unpaired custom pointers, 4: paired custom
pointers with refusal then success):

```sh
for mode in 0 1 2 3 4; do
    kos-cc -DTEST_MODE=$mode utils/net-retirement-test/pointer-target.c \
        -o /tmp/net-pointer-$mode.elf
done
```

Each should print `NETRET POINTER PASS` when run without an acquired BBA and then
remain resident. A device-absent manual-init refusal is allowed in mode 2; this
probe checks invocation/linkage and subsequent retirement, not active NIC init.
Mode 4 verifies the original EACCES refusal and then both successful custom
checks. Stop the emulator after the complete result line. A physical BBA may
correctly hold final release and is not the environment for these no-device probes.
