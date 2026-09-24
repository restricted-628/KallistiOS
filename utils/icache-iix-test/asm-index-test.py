"""Production instruction-cache range routines in a register/MMIO model.

Checks effective-address indexing, OCBWB order/operands, range admission,
P2 execution and preserved SR/stack. Not a CPU, TLB or physical cache model.
"""
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
code, labels, raw_constants = [], {}, {}
last_label = None
for line in source.splitlines():
    line = line.split('!', 1)[0].strip()
    if not line:
        continue
    if line.endswith(':'):
        last_label = line[:-1]
        labels[last_label] = len(code)
    elif line.startswith('.long'):
        raw_constants[last_label] = line.split(None, 1)[1]
    elif not line.startswith('.'):
        words = line.split(None, 1)
        code.append((words[0], [x.strip() for x in words[1].split(',')] if len(words) > 1 else []))
MASK = 0xffffffff
BASE = 0x8c000000
constants = {}
for name, value in raw_constants.items():
    if value in labels:
        constants[name] = BASE + labels[value] * 4
    elif value.startswith('~'):
        constants[name] = ~int(value[1:], 0) & MASK
    else:
        constants[name] = int(value, 0) & MASK


def execute(name, start, length, ccr):
    regs = [0x12340000 + i for i in range(16)]
    regs[4:6] = [start, length]
    regs[15] = 0x8cfffff0
    saved = regs.copy()
    original_sr = 0x40000001
    sr, flag, p2 = original_sr, False, False
    stack, events = {}, []
    ccr_reads, last_array_pc, since_array = 0, None, 0
    pc = labels[name]

    def val(a):
        return int(a[1:], 0) & MASK if a.startswith('#') else regs[int(a[1:])]

    def step(op, args):
        nonlocal sr, flag, ccr_reads, last_array_pc, since_array
        since_array += 1
        if op == 'nop':
            return
        if op == 'mov.l':
            a, b = args
            if b == '@-r15':
                regs[15] -= 4
                stack[regs[15]] = val(a)
            elif a == '@r15+':
                regs[int(b[1:])] = stack.pop(regs[15])
                regs[15] += 4
            elif a.startswith('@'):
                assert val(a[1:]) == 0xff00001c and p2 and sr & 0x10000000
                regs[int(b[1:])] = ccr
                ccr_reads += 1
            elif b.startswith('@'):
                address = val(b[1:])
                assert p2 and sr & 0x10000000
                assert address & ~0x1fe0 == 0xf0000000, hex(address)
                assert not val(a) & 1, 'entry must be invalidated'
                events.append(('I', address))
                last_array_pc, since_array = pc, 0
            else:
                regs[int(b[1:])] = constants[a]
        elif op == 'stc':
            assert args[0] == 'sr'
            regs[int(args[1][1:])] = sr
        elif op == 'ldc':
            assert args[1] == 'sr'
            sr = val(args[0])
        elif op == 'ocbwb':
            assert p2 and sr & 0x10000000
            events.append(('D', val(args[0][1:])))
        elif op == 'shlr8':
            regs[int(args[0][1:])] >>= 8
        elif op == 'shld':
            shift, reg = val(args[0]), int(args[1][1:])
            if shift & 0x80000000:
                count = (-shift) & 31
                regs[reg] = regs[reg] >> count if count else 0
            else:
                regs[reg] = (regs[reg] << (shift & 31)) & MASK
        elif op in ('mov', 'add', 'and', 'or', 'xor', 'tst', 'cmp/eq', 'cmp/hs'):
            a, b = val(args[0]), val(args[1])
            if op == 'tst': flag = not (a & b)
            elif op == 'cmp/eq': flag = a == b
            elif op == 'cmp/hs': flag = b >= a
            else:
                regs[int(args[1][1:])] = {'mov': lambda:a, 'add':lambda:a+b,
                    'and':lambda:a&b, 'or':lambda:a|b, 'xor':lambda:a^b}[op]() & MASK
        else:
            raise AssertionError(('unsupported instruction', op, args))

    for _ in range(200000):
        op, args = code[pc]
        pc += 1
        if op in ('bt', 'bf'):
            since_array += 1
            if flag == (op == 'bt'): pc = labels[args[0]]
        elif op in ('bra', 'jmp', 'rts'):
            if op == 'jmp':
                assert val(args[0][1:]) >> 29 == 5
                target = (val(args[0][1:]) - (BASE | 0x20000000)) // 4
                p2 = True
            elif op == 'bra': target = labels[args[0]]
            else: target = None
            delay_op, delay_args = code[pc]
            assert not (delay_op == 'mov.l' and not delay_args[0].startswith(('@', 'r')))
            step(delay_op, delay_args)
            if target is None:
                assert sr == original_sr and regs[8:] == saved[8:] and not stack
                assert last_array_pc is None or since_array >= 8
                return events, ccr_reads
            pc = target
        else:
            step(op, args)
    raise AssertionError('nonterminating range')


cases = []
for high in (0, 1):
    for entry in range(256):
        for base in (0x10000000, 0x8c000000, 0xac000000, 0xc0000000):
            cases.append((base | high << 25 | entry << 5, 32))
for base in (0x10000000, 0x8c000000, 0xac000000):
    for offset in (0xfff, 0x1fff, 0x1ffffff):
        for size in (1, 2, 32, 33, 65, 8193):
            cases.append((base + offset, size))
cases += [(0, 0), (0xffffffff, 2), (0x9fffffff, 2), (0xbfffffff, 2)]
checked = 0
for name in ('_arch_icache_inval_range', '_arch_icache_sync_range'):
    for ccr in (0x101, 0x8101, 0x8081, 0x81a9):
        for start, length in cases:
            valid = 0 < length <= 0x20000000 - start % 0x20000000
            expected = []
            if valid:
                first = start - 0x20000000 if start >> 29 == 5 else start
                last = (first + length - 1) & ~31
                for address in range(first & ~31, last + 1, 32):
                    index = address & 0x1fe0
                    if ccr & 0x8000:
                        index = ((address >> 25) & 1) * 0x1000 + (address & 0xfe0)
                    if name.endswith('sync_range'): expected.append(('D', address))
                    expected.append(('I', 0xf0000000 + index))
            actual, reads = execute(name, start, length, ccr)
            assert actual == expected, (name, hex(start), length, hex(ccr), actual[:4], expected[:4])
            assert reads == int(valid), 'select mode once, not per line'
            checked += 1
print(f'ICACHE-IIX-ASM: PASS cases={checked} (register/MMIO model only)')
