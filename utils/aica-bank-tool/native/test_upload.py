#!/usr/bin/env python3
"""Compile actual loader against current native API declarations and fake services."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from differential import records

r = Path(__file__).resolve().parent
root = r.parents[2]
header = (root / 'kernel/arch/dreamcast/include/dc/spu.h').read_text()
start = header.index('typedef enum spu_transfer_direction')
end = header.index('int spu_transfer_destroy(')
end = header.index(';', end) + 1
with tempfile.TemporaryDirectory(prefix='abk-upload-') as temp:
    t = Path(temp)
    (t / 'dc/sound').mkdir(parents=True)
    (t / 'kos').mkdir()
    (t / 'dc/spu.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n' + header[start:end] + '\n')
    (t / 'dc/sound/sound.h').write_text('#include <stdint.h>\n#include <stddef.h>\nuint32_t snd_mem_malloc(size_t);\nvoid snd_mem_free(uint32_t);\n')
    (t / 'kos/irq.h').write_text('int irq_inside_int(void);\n')
    (t / 'bank.abk').write_bytes(records(3,3,30000))
    cmd = [os.environ.get('CC','cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
           '-Werror', '-Wconversion', '-pedantic', '-I'+str(t),
           str(r/'abk.c'), str(r/'abk_upload.c'), str(r/'test_upload.c'),
           '-o', str(t/'test')]
    if '--sanitize' in sys.argv:
        cmd += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    print(' '.join(cmd), flush=True)
    subprocess.run(cmd, check=True)
    subprocess.run([str(t/'test'), str(t/'bank.abk')], check=True)
