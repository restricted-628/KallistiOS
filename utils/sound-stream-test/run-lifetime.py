#!/usr/bin/env python3
"""Exact source slices, deterministic external dependencies, no runtime edits."""
from pathlib import Path
import hashlib
import json
import subprocess
import os
import shlex
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
manifest = {}
def function(relative, signature):
    data = (ROOT / relative).read_bytes()
    manifest[relative] = hashlib.sha256(data).hexdigest()
    text = data.decode(); start = text.index(signature); end = text.index('{', start) + 1; depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    line = text[:start].count('\n') + 1
    return f'\n#line {line} "{ROOT / relative}"\n' + text[start:end] + '\n'
stream = 'kernel/arch/dreamcast/sound/snd_stream.c'
service = 'kernel/arch/dreamcast/sound/snd_stream_service.c'
parts = [(stream, 'static uint32_t stream_time_remaining('),
         (stream, 'static int stream_wait_channels_stopped('),
         (stream, 'int snd_stream_stop_ex('), (stream, 'int snd_stream_destroy_ex('),
         (service, 'static uint32_t deadline_remaining('),
         (service, 'int snd_stream_service_destroy(')]
out = Path(os.environ.get('STREAM_LIFETIME_BUILD_DIR', str(HERE / '.lifetime-build'))).resolve(); out.mkdir(parents=True, exist_ok=True)
unit = '#include <stddef.h>\n' + (HERE / 'lifetime-prefix.c').read_text()
unit += ''.join(function(*p) for p in parts)
unit += '\n#line 1 "lifetime-cases.c"\n' + (HERE / 'lifetime-cases.c').read_text()
(out / 'stream-test.c').write_text(unit)
cc = shlex.split(os.environ.get('HOST_CC', 'cc'))
flags = shlex.split(os.environ.get('HOST_CFLAGS', '-std=gnu17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'))
command = [*cc, *flags, str(out / 'stream-test.c'),
           '-o', str(out / 'stream-test')]
print('+', ' '.join(command), flush=True)
subprocess.run(command, check=True, timeout=60)
subprocess.run([out / 'stream-test'], check=True, timeout=60)
(out / 'manifest.json').write_text(json.dumps({'sha256': manifest, 'command': command, 'slices': parts}, indent=2) + '\n')
