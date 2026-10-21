#!/usr/bin/env python3
"""Compile selected production bodies verbatim with controlled host boundaries."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
units = {
    'driver': ('kernel/arch/dreamcast/hardware/network/broadband_adapter.c', [
        'bba_global_closed', 'bba_dma_record', 'rx_reset', 'rx_finish_enq',
        'bba_rx', 'bba_dma_cb', 'bba_link_change', 'bba_irq_hnd', 'bba_rx_process', 'bba_rx_worker', 'bba_start_link_ready',
        'bba_shutdown_context_check', 'bba_if_start', 'bba_if_stop',
        'bba_shutdown_preflight', 'bba_if_init', 'bba_if_shutdown', 'bba_tx', 'bba_if_rx_poll', 'bba_init', 'bba_shutdown']),
    'network': ('kernel/net/net_core.c', ['net_shutdown_admission_closed',
        'net_init', 'net_quiesce_checked', 'net_shutdown_checked', 'net_shutdown']),
    'platform': ('kernel/arch/dreamcast/kernel/init.c', [
        'arch_net_shutdown_can_wait', 'arch_net_shutdown_wait',
        'arch_net_shutdown_preflight', 'arch_auto_shutdown', 'arch_shutdown', 'arch_exit_handler']),
    'scif': ('kernel/arch/dreamcast/hardware/scif.c', ['byte_access_available', 'scif_try_write']),
    'hardware': ('kernel/arch/dreamcast/hardware/hardware.c', ['eth_shutdown', 'hardware_shutdown']),
}
for unit, (path, names) in units.items():
    text = (root/path).read_text()
    bodies = []
    for name in names:
        hit = re.search(r'^(?:static )?(?:int|void|bool)\s+(?:__weak_symbol\s+)?'+name+r'\([^;]*?\) \{', text, re.M)
        if not hit:
            raise ValueError(name)
        end, depth = hit.end(), 1
        # Selected bodies have no brace characters in comments/string literals.
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        bodies.append(f'#line {text[:hit.start()].count(chr(10))+1} "{path}"\n'+text[hit.start():end])
    Path(unit+'.inc').write_text('\n\n'.join(bodies)+'\n')
