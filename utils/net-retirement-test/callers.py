"""Check the architecture-only ordering that host fixtures cannot execute."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
init=(root/'kernel/arch/dreamcast/kernel/init.c').read_text()
for fn,first in [('arch_shutdown','_fini();'),('arch_auto_shutdown','dcload_syscall_net_shutdown();'),('arch_exit_handler','dbglog(')]:
    start=init.index(' '+fn+'(')
    body=init[start:]
    assert body.index('arch_net_shutdown_preflight()') < body.index(first),fn
hardware=(root/'kernel/arch/dreamcast/hardware/hardware.c').read_text()
for fn,first in [('eth_shutdown','la_shutdown();'),('hardware_shutdown','switch(initted)')]:
    body=hardware[hardware.index('void '+fn+'('):]
    assert body.index('arch_net_shutdown_preflight()') < body.index(first),fn
ex=(root/'kernel/arch/dreamcast/kernel/exec.c').read_text()
body=ex[ex.index('void arch_exec_at('):ex.index('void arch_exec(')]
assert body.count('arch_shutdown();')==1
assert body.index('arch_shutdown();') < body.index('irq_disable();') < body.index('dcache_wback_range(')
print('callers: checked barrier precedes destructive operations and exec IRQ masking')
