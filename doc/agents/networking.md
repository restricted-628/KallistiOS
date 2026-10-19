# Network drivers and NetBSD stack adaptation

Task guide for KOS Experimental. Read root `AGENTS.md` first; this is a
routing aid, not permission for broad changes. Owning lane: **Networking/BBA**.

## Scope and entry points

KOS netif/driver lifecycle, BBA first, optional future adapters and the bounded NetBSD port.

- `kernel/net/net_core.c`
- `kernel/arch/dreamcast/hardware/network/broadband_adapter.c`
- `kernel/arch/dreamcast/hardware/network/lan_adapter.c`
- `kernel/arch/dreamcast/hardware/network/w5500_adapter.c`
- `kernel/arch/dreamcast/hardware/modem`
- `addons/libppp`
- `addons/libnetbsd`
- `utils/netbsd-pool-test`

Relevant existing documentation (dated claims must be rechecked):

- `doc/background-execution-audit.md`
- `doc/gaps-ownership.md`

## Preserve these boundaries

- Implementation is on hold pending the bounded private driver/port reconciliation. The pool/mbuf subset is not a compiled network protocol stack.
- Retain KOS scheduling/VFS/platform services. Translate socket constants, layouts, errors, options and readiness; never cast NetBSD ABI objects into KOS public objects.
- Bound memory; specify mbuf references, exhaustion, WAIT semantics, timer draining, RX context and close/dup/cancel. Do not use success-shaped no-op compatibility stubs.
- BBA first; LAN/PPP later; IPv6 is in scope. Router/workstation facilities are deferred. Platform configuration is not protocol code and networking must not require the Service Executor.

## Coordinate before changing

- VFS descriptors
- GAPS/loader, IRQ/G2 and storage
- Fibers for optional nonblocking waits
- Flash settings

## Validation entry points

Host test directories with inspected Makefile targets:

- `utils/netbsd-pool-test` — candidate `make -C utils/netbsd-pool-test test` in an assigned checkout.

Choose an appropriate target probe for the assigned change; do not invent one as already passing.

These are entry points, not a request to run all tests or a claim of success.
Build only in an assigned isolated checkout. Record the exact compiler/flags,
revision and result. Separate host, target-link, emulator and hardware evidence.

## Known gate and task handoff

Private audit locations come from the external control folder. Do not copy vendor code or publish research/local paths.

Before editing, state the bounded bug/feature, file scope, owning checkout,
related decision IDs in `notes.md`, and interface partners. On completion or
interruption, update the assigned private handoff with evidence, remaining dirty
work and one next action. Propose central decision/changelog updates to
integration; do not silently rewrite another subsystem's contract.
