#!/usr/bin/env python3
"""Build original fixed-P0 members and a resident executable-VM primitive fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True, help='new output directory')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=False)
tool = Path(os.environ['KOS_CC_BASE']) / 'bin'
prefix = os.environ.get('KOS_CC_PREFIX', 'sh-elf') + '-'
commands = []
def run(cmd):
    cmd = list(map(str, cmd)); commands.append(cmd)
    print('$', ' '.join(cmd), flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    print(r.stdout + r.stderr, end='', flush=True)
    r.check_returncode()
    return r.stdout
members = []
for number, letter in ((1, 'A'), (2, 'B')):
    obj, elf, binary = (out / (letter + suffix) for suffix in ('.o', '.elf', '.BIN'))
    run([tool / (prefix + 'gcc'), '-std=gnu17', '-O2', '-ml', '-m4-single',
         '-Wa,--isa=sh4', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
         '-fno-pic', '-fno-PIE', '-fno-common', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-optimize-sibling-calls',
         '-DMEMBER_ID=' + str(number), '-c', HERE / 'member.c', '-o', obj])
    run([tool / (prefix + 'ld'), '-m', 'shlelf', '--fatal-warnings',
         '-T', HERE / 'member.ld', obj, '-o', elf])
    assert not run([tool / (prefix + 'nm'), '-u', elf]).strip(), 'unresolved member symbol'
    symbols = run([tool / (prefix + 'nm'), '-n', elf])
    (out / (letter + '.nm')).write_text(symbols)
    for n, symbol in enumerate(('_member_entry', '_page1', '_page2', '_page3', '_page4')):
        assert f'{0x101ff000 + n * 4096:08x} ' in next(line for line in symbols.splitlines() if line.endswith(' ' + symbol))
    headers = run([tool / (prefix + 'readelf'), '-h', '-S', '-r', elf])
    (out / (letter + '.readelf')).write_text(headers)
    assert 'There are no relocations in this file.' in headers
    disassembly = run([tool / (prefix + 'objdump'), '-d', elf])
    (out / (letter + '.dis')).write_text(disassembly)
    run([tool / (prefix + 'objcopy'), '-O', 'binary', elf, binary])
    raw = binary.read_bytes()
    assert len(raw) == 5 * 4096 + 4
    members.append(raw)
# Byte-for-byte identity is sufficient for this primitive original fixture.
# Package parsing/digest policy belongs to the later overlay consumer.
identity = out / 'identity.c'
identity.write_text('const unsigned char expected_members[2][20484] = {\n' +
                    ',\n'.join('{' + ','.join(map(str, raw)) + '}' for raw in members) + '\n};\n')
objects = []
for src in (HERE / 'resident-exec.c', identity):
    obj = out / (src.stem + '.o')
    run(['kos-cc', '-std=gnu17', '-Wextra', '-Werror', '-c', src, '-o', obj])
    objects.append(obj)
elf = out / 'resident-exec.elf'
run(['kos-cc', '-Wl,-Map,' + str(out / 'resident-exec.map'), '-o', elf, *objects])
run([tool / (prefix + 'objcopy'), '-O', 'binary', elf, out / 'resident-exec.bin'])
(out / 'resident-exec.nm').write_text(run([tool / (prefix + 'nm'), '-n', elf]))
record = {'base': '0x101ff000', 'pages': 6, 'page_plan': [5,5,5,5,5,6],
          'compiler': run([tool / (prefix + 'gcc'), '--version']).splitlines()[0],
          'commands': commands, 'runtime_executed': False,
          'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in out.iterdir() if p.is_file()}}
(out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS: original fixed-P0 members, five instruction pages, data/BSS, resident link')
