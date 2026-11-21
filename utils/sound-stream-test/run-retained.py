#!/usr/bin/env python3
"""Compile the production retained-stream and DMA bodies with controlled I/O."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
audio = here.parents[1] / "kernel/arch/dreamcast/sound"
source = (audio / "snd_stream.c").read_text()
dma = source[source.index("static void stream_dma_finish("):
             source.index("/* Fill one per-channel interval", source.index("static void stream_dma_finish("))]
retained = source[source.index("/* Retained PCM uses"):]
flags = os.environ.get("HOST_CFLAGS", "-O1 -g -fsanitize=address,undefined")
with tempfile.TemporaryDirectory(prefix="kos-retained-pcm-") as temp:
    unit = Path(temp) / "test.c"
    unit.write_text(
        (here / "retained-prefix.c").read_text() +
        '\n#line 1 "snd_stream DMA"\n' + dma +
        '\n#line 1 "snd_stream retained"\n' + retained +
        '\n#line 1 "retained-cases.c"\n' + (here / "retained-cases.c").read_text())
    target = Path(temp) / "test"
    command = [*shlex.split(os.environ.get("HOST_CC", "cc")),
               "-std=gnu17", "-Wall", "-Wextra", "-Werror",
               *shlex.split(flags), "-I", str(audio),
               "-include", str(here.parent / "sound-observation-test/stubs/types.h"),
               "-I", str(audio.parent / "include"), str(unit), "-o", str(target)]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([target], check=True, timeout=60)
