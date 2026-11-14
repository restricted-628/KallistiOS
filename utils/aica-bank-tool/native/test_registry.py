#!/usr/bin/env python3
"""Exercise actual ARM registry and SH4 owner with model transport, not hardware."""
from pathlib import Path
import os, subprocess, sys, tempfile
from differential import records
r=Path(__file__).resolve().parent
root=r.parents[2]
header=(root/'kernel/arch/dreamcast/include/dc/spu.h').read_text()
a=header.index('typedef enum spu_transfer_direction')
b=header.index(';',header.index('int spu_transfer_destroy('))+1
with tempfile.TemporaryDirectory(prefix='abk-registry-') as temp:
    t=Path(temp)
    (t/'dc/sound').mkdir(parents=True)
    (t/'kos').mkdir()
    (t/'dc/spu.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n'+header[a:b]+'\n')
    (t/'dc/sound/sound.h').write_text('#include <stdint.h>\n#include <stddef.h>\nuint32_t snd_mem_malloc(size_t);\nvoid snd_mem_free(uint32_t);\n')
    (t/'kos/irq.h').write_text('int irq_inside_int(void);\n')
    (t/'kos/timer.h').write_text('#include <stdint.h>\nuint64_t timer_ms_gettime64(void);\n')
    (t/'kos/mutex.h').write_text('#include <pthread.h>\ntypedef pthread_mutex_t mutex_t;\n#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER\n#define mutex_lock pthread_mutex_lock\n#define mutex_unlock pthread_mutex_unlock\n')
    (t/'snd_iface_internal.h').write_text('#include <stdint.h>\nuint32_t snd_iface_observation_features(void);\nint snd_observation_session(uint32_t *);\nint snd_observation_bind_until(uint64_t,uint32_t *);\nint snd_iface_observation_read(uint32_t,void *,uint32_t);\nint snd_iface_observation_submit(void *,uint32_t,uint64_t);\n')
    (t/'bank.abk').write_bytes(records(3,3,100))
    (t/'max.abk').write_bytes(records(256,1024,1))
    cmd=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wconversion','-pedantic','-pthread',
         '-I'+str(t),'-I'+str(root/'kernel/arch/dreamcast/include'),'-I'+str(root/'kernel/arch/dreamcast/sound/arm'),
         '-include',str(root/'utils/sound-observation-test/stubs/types.h'),
         *[str(r/f) for f in ['abk.c','abk_upload.c','abk_registry.c','test_registry.c']],
         str(root/'kernel/arch/dreamcast/sound/arm/aica_bank_registry.c'),'-o',str(t/'test')]
    if '--sanitize' in sys.argv: cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    print(' '.join(cmd),flush=True)
    subprocess.run(cmd,check=True)
    subprocess.run([str(t/'test'),str(t/'bank.abk'),str(t/'max.abk')],check=True)
    subprocess.run([str(t/'test'),str(t/'bank.abk'),str(t/'max.abk'),'quiesce'],check=True)
    for fault in range(17):
        subprocess.run([str(t/'test'),str(t/'bank.abk'),str(t/'max.abk'),'fault',str(fault)],check=True)
