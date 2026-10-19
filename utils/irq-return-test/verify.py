#!/usr/bin/env python3
"""Check emitted SH-4 return routing/guard; does not execute SH instructions."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def check(source, cc, directory):
    source_file = directory / 'entry.s'
    obj = directory / 'entry.o'
    raw = directory / 'entry.bin'
    source_file.write_text(source)
    prefix = cc.removesuffix('gcc')
    subprocess.run([cc, '-ml', '-m4-single', '-x', 'assembler', '-c',
                    str(source_file), '-o', str(obj)], check=True)
    subprocess.run([prefix + 'objcopy', '-O', 'binary', '-j', '.text',
                    str(obj), str(raw)], check=True)
    symbols = {}
    for line in subprocess.check_output([prefix + 'nm', '-n', str(obj)], text=True).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = int(fields[0], 16)
    data = raw.read_bytes()
    word = lambda p: struct.unpack_from('<H', data, p)[0]
    force = symbols['_irq_force_return']
    finish = symbols['_save_regs_finish']
    rte = data.index(b'\x2b\x00\x09\x00', finish)
    errors = []
    # The normal C-call return is jsr @r2; nop; bra destination; nop.
    calls = [p for p in range(symbols['_irq_save_regs'], force - 6, 2)
             if word(p) == 0x420b and word(p + 2) == 0x0009
             and word(p + 4) >> 12 == 0xa and word(p + 6) == 0x0009]
    if len(calls) != 1:
        errors.append('normal post-C route not uniquely identified')
    else:
        branch = calls[0] + 4
        disp = word(branch) & 0xfff
        if disp & 0x800:
            disp -= 0x1000
        if branch + 4 + 2 * disp != force:
            errors.append('normal return bypasses guard')
    if finish != force + 8 or word(force) >> 8 != 0xd1:
        errors.append('guard is not the shared four-instruction sequence')
    else:
        literal = ((force + 4) & ~3) + 4 * (word(force) & 0xff)
        mask = struct.unpack_from('<I', data, literal)[0]
        if mask != 0x30000000:
            errors.append('guard does not set exactly BL|RB')
        if [word(force + i) for i in (2, 4, 6)] != [0x0002, 0x201b, 0x400e]:
            errors.append('guard does not preserve other live SR bits')
    restore = [word(p) for p in range(finish, rte, 2)]
    if restore.count(0x4147) != 1 or restore.count(0x4137) != 1:
        errors.append('SPC/SSR not restored once from saved context')
    if 0x400e in restore or 0x410e in restore:
        errors.append('unexpected live SR write after guard')
    if restore[:3][1:] != [0xe210, 0x4147]:
        errors.append('first SPC restore boundary changed')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        default=Path(__file__).resolve().parents[2] /
                        'kernel/arch/dreamcast/kernel/entry.s')
    parser.add_argument('--cc', default=os.environ.get('KOS_CC', 'sh-elf-gcc'))
    args = parser.parse_args()
    source = args.source.read_text()
    with tempfile.TemporaryDirectory(prefix='irq-return-') as scratch:
        errors = check(source, args.cc, Path(scratch))
        if errors:
            raise SystemExit('FAIL: ' + '; '.join(errors))
        # Both half-fixes and the old combined defect must be detected.
        for name, body in [
            ('normal bypass', source.replace('bra\t\t_irq_force_return',
                                              'bra\t\t_save_regs_finish', 1)),
            ('RB-only mask', source.replace('.long\t0x30000000', '.long\t0x20000000', 1)),
            ('both defects', source.replace('bra\t\t_irq_force_return',
                                             'bra\t\t_save_regs_finish', 1)
                                  .replace('.long\t0x30000000', '.long\t0x20000000', 1))]:
            if body == source or not check(body, args.cc, Path(scratch)):
                raise SystemExit('FAIL: regression missed ' + name)
            print('negative control detected:', name)
    print('PASS: emitted common BL|RB guard, normal/forced routes and saved SPC/SSR loads')


if __name__ == '__main__':
    main()
