"""Compile the production table-management section, not a copied algorithm."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cc', default='cc')
p.add_argument('--cflags', default='-std=gnu17 -O2 -Wall -Wextra -Werror')
p.add_argument('--source', type=Path)
a = p.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
source = (a.source or root / 'kernel/arch/dreamcast/kernel/mmu.c').read_text()

def between(start, end):
    assert source.count(start) == 1 and end in source.split(start, 1)[1]
    return source.split(start, 1)[1].split(end, 1)[0]

# These sections are compiled verbatim. Omit SH-4-only MMIO, exception, and
# copy routines; substitute allocation, IRQ exclusion and retirement hooks.
defines = '#define MMU_IND_BITS' + between('#define MMU_IND_BITS', '/********************************************************************************/')
pte = '#define BUILD_PTEH' + between('#define BUILD_PTEH', '#define SET_TTB')
table = '/* Table management */' + between('/* Table management */', '#if 0   /* Only applies to KOS-MMU */')
static_map = 'static const unsigned int page_mask[]' + between(
    'static const unsigned int page_mask[]', '\nvoid mmu_init_basic(void)')
mmucr = '#define SET_MMUCR' + between('#define SET_MMUCR', '/********************************************************************************/')
with tempfile.TemporaryDirectory(prefix='kos-mmu-page-test.') as td:
    tmp = Path(td)
    (tmp / 'kos').mkdir()
    (tmp / 'kos/cdefs.h').write_text('#include <sys/cdefs.h>\n')
    (tmp / 'production.inc').write_text(defines + pte + mmucr + table + static_map)
    exe = tmp / 'test'
    subprocess.run(shlex.split(a.cc) + shlex.split(a.cflags) + [
        '-I' + str(tmp), '-I' + str(root / 'kernel/arch/dreamcast/include'),
        str(here / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
