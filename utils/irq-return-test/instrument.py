#!/usr/bin/env python3
"""Generate a diagnostic entry object; never change the production source."""
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
anchor = '_save_regs_finish:\n'
pool = '_irqfr_or:\n'
if source.count(anchor) != 1 or source.count(pool) != 1:
    raise SystemExit('entry layout changed; review diagnostic placement')
# R0-R2 are scratch here and the following restore overwrites them. No call,
# stack use, status write or FP instruction occurs in the observer.
observer = '''
\tstc\tsr,r0
\tmov.l\t_probe_observations_addr,r1
\tmov.l\tr0,@r1
\tmov.l\t@(8,r1),r2
\tand\tr0,r2
\tmov.l\tr2,@(8,r1)
\tmov.l\t@(4,r1),r0
\tadd\t#1,r0
\tmov.l\tr0,@(4,r1)
'''
source = source.replace(anchor, anchor + observer)
source = source.replace(pool, '_probe_observations_addr:\n\t.long\t_probe_observations\n' + pool)
Path(sys.argv[2]).write_text(source)
