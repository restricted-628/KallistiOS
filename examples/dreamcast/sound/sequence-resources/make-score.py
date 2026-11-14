from pathlib import Path
import sys
root=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(root/'utils/aica-sequence-tool'))
import asq
from test_asq import score
manifest=score()
manifest['programs'].append({'tpqn':96,'tempo_us':500000,'events':[
    {'delta':0,'kind':'midi','status':0x90,'a':64,'b':90},
    {'delta':96,'kind':'end'}]})
blob=asq.build(manifest)
Path('score.h').write_text('static const unsigned char score[] __attribute__((aligned(32))) = {'+','.join(map(str,blob))+'};\n')
