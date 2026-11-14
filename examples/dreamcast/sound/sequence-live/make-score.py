from pathlib import Path
import sys
root=Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root/'utils/aica-sequence-tool'))
import asq
hold = [{'delta': 0, 'kind': 'midi', 'status': 0x90, 'a': 40+i, 'b': 100} for i in range(20)]
hold.append({'delta': 960, 'kind': 'end'})
loop = [{'delta': 0, 'kind': 'loop', 'count': 0},
        {'delta': 0, 'kind': 'midi', 'status': 0x90, 'a': 64, 'b': 100},
        {'delta': 96, 'kind': 'midi', 'status': 0x80, 'a': 64, 'b': 0},
        {'delta': 96, 'kind': 'loop_end'}, {'delta': 0, 'kind': 'end'}]
short = [{'delta': 0, 'kind': 'midi', 'status': 0x90, 'a': 72, 'b': 100},
         {'delta': 48, 'kind': 'midi', 'status': 0x80, 'a': 72, 'b': 0},
         {'delta': 0, 'kind': 'end'}]
crowded = [{'delta': 0, 'kind': 'midi', 'status': 0xb0, 'a': 7, 'b': i} for i in range(96)]
crowded.append({'delta': 96, 'kind': 'end'})
programs = [{'tpqn': 96, 'tempo_us': 500000, 'events': e} for e in (hold, loop, short, crowded)]
blob = asq.build({'programs': programs})
Path('score.h').write_text('static const unsigned char score[] __attribute__((aligned(32))) = {' + ','.join(map(str, blob)) + '};\n')
