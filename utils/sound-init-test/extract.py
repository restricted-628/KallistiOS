"""Compile exact startup/queue bodies; fake only OS, MMIO and query transport."""
from pathlib import Path
import hashlib
import re
source = Path('../../kernel/arch/dreamcast/sound/snd_iface.c').read_text()
parts = []
for name in ['queue_geometry', 'shared_queue_state', 'remaining_timeout', 'snd_init']:
    match = re.search(r'^(?:static )?(?:int|uint32_t) '+name+r'\([^;]+?\) \{',source,re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[match.start():end]
    parts.append(body)
    print(name, hashlib.sha256(body.encode()).hexdigest())
Path('startup.inc').write_text('\n\n'.join(parts)+'\n')
