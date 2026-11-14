#!/usr/bin/env python3
"""Compile actual host owner against real firmware dispatch, fake OS/transport."""
from pathlib import Path
import subprocess, tempfile, sys, os
root=Path(__file__).resolve().parents[2]
arm=root/'kernel/arch/dreamcast/sound/arm'
with tempfile.TemporaryDirectory(prefix='music-owner-') as d:
    t=Path(d)
    (t/'dc').mkdir();(t/'arch').mkdir()
    header=(root/'kernel/arch/dreamcast/include/dc/spu.h').read_text()
    a=header.index('typedef enum spu_transfer_direction');b=header.index(';',header.index('int spu_transfer_destroy('))+1
    (t/'dc/spu.h').write_text('#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n'+header[a:b]+'\n')
    (t/'arch/timer.h').write_text('#include <stdint.h>\nuint64_t timer_ms_gettime64(void);\n')
    (t/'snd_iface_internal.h').write_text('#include <stdint.h>\nuint32_t snd_iface_observation_features(void);\nint snd_observation_session(uint32_t *);\nint snd_observation_bind_until(uint64_t,uint32_t *);\nint snd_iface_observation_read(uint32_t,void *,uint32_t);\nint snd_iface_observation_submit(void *,uint32_t,uint64_t);\n')
    (t/'snd_port_internal.h').write_text('#include <dc/sound/port.h>\nint snd_port_playback_pin(snd_port_t,unsigned *);\nint snd_port_playback_unpin(snd_port_t);\n')
    cmd=[os.environ.get('CC','cc'),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-finstrument-functions']
    for p in [root/'utils/aica-sequence-tool/native',t,root/'utils/sound-observation-test/stubs',arm,root/'kernel/arch/dreamcast/include',root/'kernel/arch/dreamcast/include/dc/sound',root/'utils/aica-bank-tool/native',root/'utils/aica-music-client']: cmd+=['-I'+str(p)]
    cmd+=['-include',str(root/'utils/sound-observation-test/stubs/types.h')]
    cmd += [str(root/'utils/sound-music-live-test/host.c'),str(root/'utils/aica-music-client/music_live.c'),str(root/'utils/aica-music-client/music_client.c')]
    cmd += [str(arm/(s+'.c')) for s in ['aica_observer','aica_bank_registry','aica_music','aica_music_bank','aica_music_wire','aica_music_live']]
    cmd += [str(root/'utils/aica-sequence-tool/native'/(s+'.c')) for s in ['asq','catalog','wire','live']]
    cmd += [str(arm/(s+'.c')) for s in ['aica_sequence','aica_sequence_resource','aica_music_sequence']]
    cmd += ['-o',str(t/'test')]
    if '--sanitize' in sys.argv: cmd += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(cmd,check=True)
    for n in range(11): subprocess.run([str(t/'test'),str(n)],check=True)
