"""Check the architecture-only ordering that host fixtures cannot execute."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
init=(root/'kernel/arch/dreamcast/kernel/init.c').read_text()
for fn,first in [('arch_shutdown','_fini();'),('arch_auto_shutdown','dcload_syscall_net_shutdown();'),('arch_exit_handler','dbglog(')]:
    start=init.index(' '+fn+'(')
    body=init[start:]
    assert body.index('arch_shutdown_prepare();') < body.index('arch_net_shutdown_preflight()') < body.index(first),fn
hardware=(root/'kernel/arch/dreamcast/hardware/hardware.c').read_text()
for fn,first in [('eth_shutdown','la_shutdown();'),('hardware_shutdown_common','spu_transfer_system_shutdown()')]:
    body=hardware[hardware.index('void '+fn+'('):]
    assert body.index('arch_net_shutdown_preflight()') < body.index(first),fn
body=hardware[hardware.index('void hardware_shutdown_common('):]
assert body.index('snd_mem_reconfigure_begin()') < body.index('arch_net_shutdown_preflight()')
assert (body.index('spu_transfer_system_shutdown()') < body.index('g2_dma_shutdown_checked()')
        < body.index('spu_sound_shutdown_in_transition()') < body.index('spu_clear_retired()'))
body=init[init.index('void  __weak_symbol arch_auto_shutdown('):]
assert body.index('spu_transfer_system_shutdown()') < body.index('fs_fdtbl_destroy();')
assert body.index('spu_transfer_system_shutdown()') < body.index('hardware_shutdown_after_spu_retired();')
newlib=(root/'kernel/libc/newlib/newlib_exit.c').read_text()
body=newlib[newlib.index('void exit('):]
assert body.index('arch_shutdown_prepare();') < body.index('__call_exitprocs(code, NULL);')
assert body.index('__call_exitprocs(code, NULL);') < body.index('__stdio_exit_handler();') < body.index('_exit(code);')
assert init.index('hardware_periph_init_boot();') < init.index('irq_enable();')
ex=(root/'kernel/arch/dreamcast/kernel/exec.c').read_text()
body=ex[ex.index('void arch_exec_at('):ex.index('void arch_exec(')]
assert body.count('arch_shutdown();')==1
assert body.index('arch_shutdown();') < body.index('irq_disable();') < body.index('dcache_wback_range(')
print('callers: checked barrier precedes destructive operations and exec IRQ masking')
