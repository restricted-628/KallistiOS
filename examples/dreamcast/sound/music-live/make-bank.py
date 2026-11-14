#!/usr/bin/env python3
"""Build a header from the fork's original triangle-wave authoring fixture."""
import importlib.util, json, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[4]
def module(name):
    spec=importlib.util.spec_from_file_location(name,root/'utils/aica-bank-tool'/f'{name}.py')
    out=importlib.util.module_from_spec(spec);spec.loader.exec_module(out);return out
with tempfile.TemporaryDirectory(prefix='music-live-bank-') as t:
    directory=Path(t)/'original'
    module('fixture').create(directory)
    blob=module('aica_bank').build(json.loads((directory/'bank.json').read_text()),directory)
    text='/* Generated original fixture; regenerate with make-bank.py. */\nstatic const unsigned char bank[] __attribute__((aligned(32))) = {\n'
    for start in range(0,len(blob),16): text+=','.join(f'0x{b:02x}' for b in blob[start:start+16])+',\n'
    Path('bank.h').write_text(text+'};\n')
