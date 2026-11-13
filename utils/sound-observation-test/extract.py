#!/usr/bin/env python3
"""Compile exact production bridge bodies, with only hardware/OS dependencies faked."""
from pathlib import Path
import hashlib
import re
source = Path('../../kernel/arch/dreamcast/sound/snd_iface.c').read_text()
names = ['remaining_timeout', 'channel_fields_valid', 'channel_config_pack',
         'snd_channel_config_validate', 'snd_iface_observation_features',
         'snd_iface_observation_validate', 'snd_iface_observation_pack',
         'snd_iface_observation_submit', 'snd_iface_observation_read']
parts = []
for name in names:
    m = re.search(r'^(?:static )?(?:int|uint32_t|void) ' + name + r'\([^;]+?\) \{', source, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[m.start():end]
    parts.append(body)
    print(name, hashlib.sha256(body.encode()).hexdigest())
Path('bridges.inc').write_text('\n\n'.join(parts) + '\n')
