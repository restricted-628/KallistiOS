#!/usr/bin/env python3
"""Compile unchanged production requests + target probe with explicit host shims."""
import hashlib
import json
from pathlib import Path
import os
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
OUT = Path(os.environ.get('SPU_HOST_BUILD_DIR', str(HERE / 'build'))).resolve()
OUT.mkdir(parents=True, exist_ok=True)
manifest = {}
def source(relative):
    path = ROOT / relative
    data = path.read_bytes()
    manifest[relative] = hashlib.sha256(data).hexdigest()
    return data.decode()

def extract_function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'

for name in ['kos.h', 'arch/arch.h', 'kos/cdefs.h', 'kos/cond.h', 'kos/genwait.h',
             'kos/irq.h', 'kos/mutex.h', 'kos/sem.h', 'kos/thread.h', 'kos/timer.h',
             'kos/worker_thread.h', 'dc/memory.h']:
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "host-shim.h"\n')

g2 = source('kernel/arch/dreamcast/include/dc/g2bus.h')
g2 = g2[:g2.index('/** \\brief  G2 context')]
g2 = g2.replace('#include <dc/fifo.h>', '') + '\n__END_DECLS\n#endif\n'
(OUT / 'dc/g2bus.h').write_text(g2)
source('kernel/arch/dreamcast/include/dc/spu.h')
spu = source('kernel/arch/dreamcast/hardware/spu.c')
(OUT / 'shutdown.c').write_text('#include <dc/spu.h>\nint spu_transfer_system_shutdown(void);\n' +
                              extract_function(spu, 'int spu_shutdown(void)'))
paths = ['kernel/arch/dreamcast/hardware/spu_request.c',
         'examples/dreamcast/sound/spu-lifetime/main.c']
for p in paths:
    source(p)
source('kernel/arch/dreamcast/hardware/spu_internal.h')
manifest['worker-reference-only'] = hashlib.sha256((ROOT / 'kernel/thread/worker.c').read_bytes()).hexdigest()

commands = []
def run(args):
    commands.append([str(a) for a in args])
    print('+', ' '.join(str(a) for a in args), flush=True)
    subprocess.run(args, check=True, timeout=60)

cc = shlex.split(os.environ.get('HOST_CC', 'cc'))
run([*cc, '--version'])
flags = shlex.split(os.environ['HOST_CFLAGS']) if 'HOST_CFLAGS' in os.environ else ['-std=gnu17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
flags += ['-pthread', '-I' + str(OUT), '-I' + str(HERE),
         '-I' + str(ROOT / 'kernel/arch/dreamcast/include')]
wrap = ['-Dg2_dma_' + x + '=__wrap_g2_dma_' + x for x in ['transfer', 'get_status', 'cancel']]
objects = []
for index, (src, extra) in enumerate([(ROOT / paths[0], wrap),
                                   (ROOT / paths[1], ['-Dmain=probe_main']),
                                   (HERE / 'host-shim.c', []),
                                   (OUT / 'shutdown.c', [])]):
    obj = OUT / f'{index}.o'
    run([*cc, *flags, *extra, '-c', str(src), '-o', str(obj)])
    objects.append(str(obj))
run([*cc, *flags, *objects, '-o', str(OUT / 'spu-lifetime-host')])
run([str(OUT / 'spu-lifetime-host')])
(OUT / 'manifest.json').write_text(json.dumps({'sha256': manifest, 'commands': commands}, indent=2) + '\n')
