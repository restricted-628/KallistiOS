from pathlib import Path
import sys
sys.path.insert(0, '..')
import asq
from test_asq import score
blob = asq.build(score())
Path('wire-fixture.h').write_text('static const unsigned char fixture[] = {' + ','.join(map(str,blob)) + '};\n')
