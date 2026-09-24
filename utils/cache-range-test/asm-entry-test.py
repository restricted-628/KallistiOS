"""Execute production SH-4 assembly preflight in a small 32-bit register model.

Stops at the jump into P2, before cache instructions or MMIO. This verifies
admission/normalization control flow, not CPU timing or cache correctness.
Unsupported instructions fail rather than silently being treated as no-ops.
"""
from pathlib import Path
import random
import re
import sys

source = Path(sys.argv[1]).read_text()
MASK = 0xffffffff
constants = {"area_mask": 0xe0000000, "cache_mask": 0x1fffffff,
             "p1_base": 0x80000000, "p2_mask": 0xa0000000}
for label, expected in constants.items():
    match = re.search(r"^" + label + r":\s*\n\s*\.long\s+(0x[0-9a-fA-F]+)", source, re.M)
    assert match and int(match[1], 16) == expected, label
# Internal routine pointers are not dereferenced by this entry-path model.
constants.update(iir_addr=0x8c010000, ifr_addr=0x8c020000)


def program(name, end):
    body = source.split(name + ":", 1)[1].split(end + ":", 1)[0]
    code, labels = [], {}
    for line in body.splitlines():
        line = line.split("!", 1)[0].strip()
        if not line or line.startswith(".align"):
            continue
        if line.endswith(":"):
            labels[line[:-1]] = len(code)
        else:
            code.append(re.split(r"\s+", line, maxsplit=1))
    return code, labels


def execute(code, labels, start, size):
    regs = {f"r{i}": 0 for i in range(16)}
    regs.update(r4=start, r5=size)
    flag, pc = False, 0
    for _ in range(100):
        op, *tail = code[pc]
        args = [s.strip() for s in tail[0].split(",")] if tail else []
        pc += 1
        if op in ("bt", "bf"):
            if flag == (op == "bt"):
                target = args[0]
                if target.endswith("_exit"):
                    return None
                pc = labels[target]
        elif op == "jmp":
            assert code[pc] == ["nop"], "unexpected P2 jump delay slot"
            return regs["r4"], regs["r5"]
        elif op == "mov.l":
            regs[args[1]] = constants[args[0]]
        elif op in ("mov", "add", "and", "or", "xor", "tst", "cmp/hs", "cmp/eq"):
            lhs = int(args[0][1:], 0) & MASK if args[0].startswith("#") else regs[args[0]]
            rhs = regs[args[1]]
            if op == "tst":
                flag = (lhs & rhs) == 0
            elif op == "cmp/hs":
                flag = rhs >= lhs
            elif op == "cmp/eq":
                flag = rhs == lhs
            else:
                value = {"mov": lambda: lhs, "add": lambda: rhs + lhs,
                         "and": lambda: rhs & lhs, "or": lambda: rhs | lhs,
                         "xor": lambda: rhs ^ lhs}[op]()
                regs[args[1]] = value & MASK
        else:
            raise AssertionError(f"unsupported entry instruction: {op}")
    raise AssertionError("entry path did not terminate")


rng = random.Random(0x534834)
cases = [(rng.randrange(1 << 32), rng.randrange(1 << 32)) for _ in range(10000)]
for area in range(8):
    for offset in range(64):
        for count in (0, 1, 2, 31, 32, 33, 64, 65, 0x20000000, MASK):
            cases.append(((area << 29) + offset, count))
            cases.append((((area + 1) << 29) - 1 - offset, count))

checked = 0
for name, end in (("_arch_icache_inval_range", ".iinval_real"),
                  ("_arch_icache_sync_range", ".iflush_real")):
    code, labels = program(name, end)
    for start, count in cases:
        valid = 0 < count <= 0x20000000 - start % 0x20000000
        expected = ((start - 0x20000000 if start >> 29 == 5 else start), count) if valid else None
        actual = execute(code, labels, start, count)
        assert actual == expected, (name, hex(start), count, actual, expected)
        checked += 1
print(f"CACHE-ASM-ENTRY: PASS cases={checked} (control flow only)")
