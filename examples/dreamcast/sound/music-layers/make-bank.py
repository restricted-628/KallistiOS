#!/usr/bin/env python3
"""Four original triangle-wave samples; no imported sound or bank data."""
from pathlib import Path
import importlib.util,struct,tempfile,wave
root=Path(__file__).resolve().parents[4]
spec=importlib.util.spec_from_file_location('abk',root/'utils/aica-bank-tool/aica_bank.py')
abk=importlib.util.module_from_spec(spec);spec.loader.exec_module(abk)
with tempfile.TemporaryDirectory(prefix='music-layers-') as t:
    d=Path(t);samples=[];zones=[]
    for i,rate in enumerate([11025,16000,22050,32000]):
        name='tone'+str(i);pcm=[((n if n<32 else 64-n)*128-2048)*(i+1) for n in range(64)]
        with wave.open(str(d/(name+'.wav')),'wb') as wav:
            wav.setnchannels(1);wav.setsampwidth(2);wav.setframerate(rate)
            wav.writeframes(struct.pack('<64h',*pcm))
        samples.append({'name':name,'wav':name+'.wav','loop_start':0,'loop_end':64})
        zones.append({'sample':name,'layer':i,'root_key':60,'volume':100,'key_rate':15})
    blob=abk.build({'version':2,'samples':samples,'zones':zones},d)
    Path('bank.h').write_text('/* Generated original layered fixture. */\nstatic const unsigned char bank[] __attribute__((aligned(32))) = {\n'+','.join(str(b) for b in blob)+'};\n')
