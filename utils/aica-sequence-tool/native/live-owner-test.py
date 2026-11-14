"""Actual sequence owner/client/core with only OS/transport faked."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(__file__).resolve().parent;root=r.parents[2];arm=root/'kernel/arch/dreamcast/sound/arm'
header=(root/'kernel/arch/dreamcast/include/dc/spu.h').read_text()
a=header.index('typedef enum spu_transfer_direction');b=header.index(';',header.index('int spu_transfer_destroy('))+1
with tempfile.TemporaryDirectory(prefix='asql-owner-') as d:
 t=Path(d)
 for p in ['dc/sound','kos','arch']:(t/p).mkdir(parents=True,exist_ok=True)
 (t/'dc/spu.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n'+header[a:b]+'\n')
 (t/'dc/sound/sound.h').write_text('#include <stdint.h>\n#include <stddef.h>\nuint32_t snd_mem_malloc(size_t);\nvoid snd_mem_free(uint32_t);\n')
 (t/'kos/irq.h').write_text('int irq_inside_int(void);\n')
 (t/'arch/timer.h').write_text('#include <stdint.h>\nuint64_t timer_ms_gettime64(void);\n')
 (t/'snd_iface_internal.h').write_text('#include <stdint.h>\nuint32_t snd_iface_observation_features(void);\nint snd_observation_session(uint32_t *);\nint snd_observation_bind_until(uint64_t,uint32_t *);\nint snd_iface_observation_read(uint32_t,void *,uint32_t);\nint snd_iface_observation_submit(void *,uint32_t,uint64_t);\n')
 cmd=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wconversion','-pedantic','-I'+str(t),'-I'+str(arm),'-I'+str(root/'utils/aica-music-client'),'-I'+str(root/'utils/aica-bank-tool/native'),'-I'+str(root/'kernel/arch/dreamcast/include'),'-include',str(root/'utils/sound-observation-test/stubs/types.h')]
 cmd += [str(r/(name+'.c')) for name in ['live-owner-test','live-owner','live-client','live','catalog','asq']]+[str(arm/(name+'.c')) for name in ['aica_sequence','aica_music_sequence','aica_music','aica_music_wire','aica_music_bank','aica_bank_registry']]+['-o',str(t/'test')]
 if '--sanitize' in sys.argv:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(cmd,check=True)
 for mode in range(9):subprocess.run([str(t/'test'),str(mode)],check=True)
