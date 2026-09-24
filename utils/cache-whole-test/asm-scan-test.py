"""Run production P2 cache scans in a limited SH-4 register/MMIO model.

Models non-associative address-array writes and their writeback side effect,
not a full CPU, SDRAM, cache-access pipeline or hardware timing. Optional
second input exercises the integrated physical-page helper independently.
"""
from pathlib import Path
import random
import re
import sys

MASK, TAG, BASE, BL = 0xffffffff, 0x1ffffc01, 0xf4000000, 0x10000000


def parse(path, page=False):
    source = Path(path).read_text()
    literals = {name: int(value, 16) for name, value in re.findall(
        r"^([.\w]+):\s*\.long\s+(0x[0-9a-fA-F]+)", source, re.M)}
    if page:
        source = "_mmu_purge_phys_page:" + source.split("_mmu_purge_phys_page:", 1)[1].split("_cache_write_ccr:", 1)[0]
    code, labels = [], {}
    for line in source.splitlines():
        line = line.split("!", 1)[0].strip()
        if not line:
            continue
        if ":" in line:
            name, line = line.split(":", 1)
            labels[name] = len(code)
            line = line.strip()
        if not line or line.startswith((".text", ".align", ".globl", ".long")):
            continue
        fields = re.split(r"\s+", line, maxsplit=1)
        code.append((fields[0], [a.strip() for a in fields[1].split(",")] if len(fields) == 2 else []))
    return code, labels, literals


def run(program, operation, ccr, seed, saved_sr, physical=None):
    code, labels, literals = program
    page = physical is not None
    entry = "_mmu_purge_phys_page" if page else "_arch_dcache_" + operation + "_all_indexed"
    regs = {f"r{i}": 0xcafe0000 + i for i in range(16)}
    regs["r4"] = physical or 0
    original_regs = regs.copy()
    rng = random.Random(seed)
    original = [((0x0c000000 + rng.randrange(8) * 4096 + (i % 4) * 1024) |
                 ((i + seed) & 3) | (rng.getrandbits(32) & ~0x1ffffc03)) & MASK
                for i in range(512)]
    tags = original.copy()
    reads, writes, writebacks = [], [], []
    pc, sr, flag, p2, last_access = labels[entry], saved_sr, bool(saved_sr & 1), False, None

    def value(a):
        return int(a[1:], 0) & MASK if a.startswith("#") else regs[a]

    def simple(op, args, step):
        nonlocal sr, flag, last_access
        if op == "nop":
            return
        if op == "mova":
            assert args[1] == "r0"
            regs["r0"] = 0x8c010000
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
                address = regs[(src if src.startswith("@") else dst)[1:]]
                assert p2 and sr & BL, "MMIO without P2/exception exclusion"
                if src.startswith("@") and address == 0xff00001c:
                    regs[dst] = ccr
                    return
                assert BASE <= address < BASE + 16384 and address % 32 == 0, hex(address)
                index = (address - BASE) // 32
                assert not (ccr & 32 and index & 128), "OCRAM entry accessed"
                last_access = step
                if src.startswith("@"):
                    regs[dst] = tags[index]
                    reads.append(index)
                else:
                    assert regs[src] & ~TAG == 0, "reserved/U bits in write value"
                    if tags[index] & 3 == 3:
                        writebacks.append((index, tags[index] & 0x1ffffc00))
                    tags[index] = regs[src]
                    writes.append(index)
            else:
                regs[dst] = literals[src]
        elif op in ("mov", "or", "and", "add", "tst", "cmp/eq"):
            lhs, dst = value(args[0]), args[1]
            if op in ("tst", "cmp/eq"):
                flag = (lhs & regs[dst] == 0) if op == "tst" else lhs == regs[dst]
                sr = (sr & ~1) | flag
            else:
                regs[dst] = {"mov": lambda: lhs, "or": lambda: lhs | regs[dst],
                             "and": lambda: lhs & regs[dst], "add": lambda: lhs + regs[dst]}[op]() & MASK
        elif op in ("shll8", "shlr"):
            reg = args[0]
            if op == "shlr":
                flag = bool(regs[reg] & 1)
                sr = (sr & ~1) | flag
            regs[reg] = ((regs[reg] << 8) if op == "shll8" else regs[reg] >> 1) & MASK
        else:
            raise AssertionError(f"unsupported instruction: {op}")

    for step in range(15000):
        op, args = code[pc]
        pc += 1
        if op in ("bra", "jmp", "rts"):
            delay_op, delay_args = code[pc]
            # PC-relative loads have different semantics in a branch delay
            # slot. Keep them out of these helpers and this small model.
            assert delay_op not in ("mova", "bra", "jmp", "rts")
            assert not (delay_op == "mov.l" and not delay_args[0].startswith("@") and not delay_args[1].startswith("@"))
            simple(delay_op, delay_args, step)
            if op == "bra":
                pc = labels[args[0]]
            elif op == "jmp":
                assert regs[args[0][1:]] == 0xac010000
                p2 = True
                pc = labels[".mpp_p2" if page else ".all_p2"]
            else:
                assert last_access is not None and step - last_access >= 8
                assert sr == saved_sr, "status not restored"
                assert all(regs[f"r{i}"] == original_regs[f"r{i}"] for i in range(8, 16))
                break
        elif op in ("bt", "bf"):
            if flag == (op == "bt"):
                pc = labels[args[0]]
        else:
            simple(op, args, step)
    else:
        raise AssertionError("scan did not return")

    expected_reads = [i for i in range(512) if not (ccr & 32 and i & 128)]
    expected_writes = [i for i in expected_reads if original[i] & 1 and
                       (not page or original[i] & 0x1ffff000 == physical)]
    expected_tags = original.copy()
    for i in expected_writes:
        expected_tags[i] = original[i] & TAG if operation == "wback" else 0
    assert reads == expected_reads
    assert writes == expected_writes
    assert tags == expected_tags
    assert writebacks == [(i, original[i] & 0x1ffffc00) for i in expected_writes if original[i] & 2]


whole = parse(sys.argv[1])
cases = 0
for seed in range(8):
    for mode in (0, 32, 128, 160):
        for sr in (0x40000001, 0x500000f0):
            for operation in ("wback", "purge"):
                run(whole, operation, mode | 0x105, seed, sr)
                cases += 1
print(f"CACHE-WHOLE-ASM: PASS cases={cases} (register/MMIO model only)")
if len(sys.argv) > 2:
    page = parse(sys.argv[2], page=True)
    cases = 0
    for seed in range(4):
        for mode in (0, 32, 128, 160):
            for physical in (0x0c000000, 0x0c003000, 0x0c007000, 0x0d000000):
                run(page, "purge", mode | 0x105, seed, 0x40000001, physical)
                cases += 1
    print(f"CACHE-PAGE-ASM: PASS cases={cases} (register/MMIO model only)")
