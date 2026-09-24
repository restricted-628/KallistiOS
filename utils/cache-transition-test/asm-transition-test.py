"""Execute production CCR transitions in a limited SH-4 register/MMIO model.

Models tag retirement and self-clearing CCR commands, not physical cache,
scratchpad, SDRAM timing, DMA, interrupts or address translation hardware.
"""
from pathlib import Path
import itertools
import re
import sys

U32, BL, CCR, ARRAY = 0xffffffff, 0x10000000, 0xff00001c, 0xf4000000
ORA, OCI, ICI = 32, 8, 2048


def parse(source):
    literals = {name: int(value, 16) for name, value in re.findall(
        r"^([.\w]+):\s*\.long\s+(0x[0-9a-fA-F]+)", source, re.M)}
    source = source.split("_cache_write_ccr:", 1)[1].split("! Variables", 1)[0]
    code, labels = [], {}
    for line in source.splitlines():
        line = line.split("!", 1)[0].strip()
        if not line:
            continue
        if ":" in line:
            label, line = line.split(":", 1)
            labels[label] = len(code)
            line = line.strip()
        if not line or line.startswith("."):
            continue
        fields = line.split(None, 1)
        code.append((fields[0], [x.strip() for x in fields[1].split(",")]
                     if len(fields) == 2 else []))
    return code, labels, literals


def run(program, old, mask, value, saved_sr, seed):
    code, labels, literals = program
    regs = {f"r{i}": 0xface0000 + i for i in range(16)}
    regs.update(r4=mask, r5=value)
    initial_regs = regs.copy()
    original = [0x0c000000 | (i << 10) | ((i + seed) & 3) for i in range(512)]
    tags = original.copy()
    retired, dirty = [], []
    pc, sr, flag, p2, target = 0, saved_sr, bool(saved_sr & 1), False, None
    reads, writes, last_write = 0, 0, None
    expected_entries = [i for i in range(512) if not (old & ORA and i & 128)]
    expected_dirty = [i for i in expected_entries if original[i] & 3 == 3]

    def operand(a):
        return int(a[1:], 0) & U32 if a.startswith("#") else regs[a]

    def simple(op, args, step):
        nonlocal sr, flag, reads, writes, last_write, target
        if op == "nop":
            return
        if op == "mova":
            target = labels[args[0]]
            regs[args[1]] = 0x8c010000
        elif op == "stc":
            assert args[0] == "sr"
            regs[args[1]] = sr
        elif op == "ldc":
            assert args[1] == "sr"
            sr = regs[args[0]]
            flag = bool(sr & 1)
        elif op == "mov.l":
            src, dst = args
            if src.startswith("@") or dst.startswith("@"):
                assert p2 and sr & BL, "MMIO before P2/exception exclusion"
                addr = regs[(src if src.startswith("@") else dst)[1:]]
                if addr == CCR:
                    if src.startswith("@"):
                        reads += 1
                        regs[dst] = old
                    else:
                        writes += 1
                        assert retired == expected_entries, "old layout not fully retired"
                        assert dirty == expected_dirty, "dirty lines lost before CCR write"
                        expected = ((old & ~mask) | value | OCI) & U32
                        assert regs[src] == expected, "CCR formula/OCI command changed"
                        # OCI invalidates U/V in every tag; it does not write
                        # the operand-cache data array or scratchpad storage.
                        tags[:] = [tag & ~3 for tag in tags]
                        last_write = step
                else:
                    assert not writes, "retirement after mode switch"
                    assert dst.startswith("@"), "unexpected tag read"
                    assert ARRAY <= addr < ARRAY + 16384 and addr % 32 == 0
                    index = (addr - ARRAY) // 32
                    assert index in expected_entries, "active OCRAM tag accessed"
                    assert regs[src] == 0, "retirement must clear tag"
                    if tags[index] & 3 == 3:
                        dirty.append(index)
                    tags[index] = 0
                    retired.append(index)
            else:
                regs[dst] = literals[src]
        elif op in ("mov", "or", "xor", "and", "add", "tst"):
            a, dst = operand(args[0]), args[1]
            if op == "tst":
                flag = not (a & regs[dst])
                sr = (sr & ~1) | flag
            else:
                regs[dst] = {"mov": lambda: a, "or": lambda: a | regs[dst],
                             "xor": lambda: a ^ regs[dst], "and": lambda: a & regs[dst],
                             "add": lambda: a + regs[dst]}[op]() & U32
        elif op in ("shll8", "shlr", "dt"):
            reg = args[0]
            if op == "shll8":
                regs[reg] = (regs[reg] << 8) & U32
            elif op == "shlr":
                flag = bool(regs[reg] & 1)
                regs[reg] >>= 1
                sr = (sr & ~1) | flag
            else:
                regs[reg] = (regs[reg] - 1) & U32
                flag = regs[reg] == 0
                sr = (sr & ~1) | flag
        else:
            raise AssertionError(f"unsupported instruction {op}")

    for step in range(5000):
        op, args = code[pc]
        pc += 1
        if op in ("jmp", "rts"):
            assert code[pc] == ("nop", []), "unexpected delay slot"
            if op == "jmp":
                assert regs[args[0][1:]] == 0xac010000
                assert sr & BL
                p2, pc = True, target
            else:
                assert last_write is not None and step - last_write >= 8
                assert sr == saved_sr, "SR not restored"
                assert all(regs[f"r{i}"] == initial_regs[f"r{i}"] for i in range(8, 16))
                break
        elif op in ("bt", "bf"):
            if flag == (op == "bt"):
                pc = labels[args[0]]
        else:
            simple(op, args, step)
    else:
        raise AssertionError("transition did not return")
    assert reads == writes == 1
    assert all(tag & 3 == 0 for tag in tags), "stale tags survive transition"


source = Path(sys.argv[1]).read_text()
program = parse(source)
cases = 0
# Legal old modes, including disabled OC (which requires ORA=0).
modes = [0x100 | cb | wt | oix | iix | oce | ora
         for cb, wt, oix, iix, oce, ora in itertools.product(
             (0, 4), (0, 2), (0, 128), (0, 32768), (0, 1), (0, ORA))
         if oce or not ora]
for old in modes:
    for mask, value in [(0, 0), (0, ICI), (128, old ^ 128),
                        (32768, (old ^ 32768) | ICI),
                        (ORA, 0), (ORA | 1, ORA | 1),
                        (U32, 0x105), (U32, 0x105 | ORA | 128),
                        (U32, 0x100)]:
        for sr in (0x40000001, 0x500000f0):
            for seed in range(4):
                run(program, old, mask, value, sr, seed)
                cases += 1
print(f"CACHE-TRANSITION-ASM: PASS cases={cases} (register/MMIO model only)")

# Require the checks to reject common regressions in the production text.
mutations = {
    "ignore old ORA": ("and      #32, r0", "and      #0, r0"),
    "use new ORA": ("mov      r3, r0", "mov      r4, r0"),
    "omit OCI": ("or       #8, r0", "nop"),
    "omit exclusion": ("ldc      r0, sr", "nop"),
}
for name, (before, after) in mutations.items():
    prefix, body = source.split("_cache_write_ccr:", 1)
    assert body.count(before) == 1, name
    try:
        run(parse(prefix + "_cache_write_ccr:" + body.replace(before, after)),
            0x125, ORA, 0, 0x40000001, 0)
    except AssertionError:
        pass
    else:
        raise AssertionError(f"negative control survived: {name}")
print(f"CACHE-TRANSITION-ASM: negative controls rejected={len(mutations)}")
