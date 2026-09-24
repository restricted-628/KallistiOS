"""Execute the production invalidator in a limited SH-4/MMIO register model.

The associative-match rule follows Renesas manual section 3.7.4: the VPN is
in the write value, but ASID is in PTEH. This is not a CPU, timing, cache, or
hardware emulator. Unknown instructions and unexpected MMIO fail closed.
"""
from pathlib import Path
import random
import re
import sys

source = Path(sys.argv[1]).read_text()
body = source.split("_mmu_invalidate_tlb:", 1)[1].split("inval_real_addr:", 1)[0]
constants = {}
for name, value in re.findall(r"^(\w+):\s*\.long\s+(0x[0-9a-fA-F]+)", source, re.M):
    constants[name] = int(value, 16)
constants["inval_real_addr"] = 0x8c010040
code, labels = [], {}
for line in body.splitlines():
    line = line.split("!", 1)[0].strip()
    if not line or line.startswith(".align"):
        continue
    if line.endswith(":"):
        labels[line[:-1]] = len(code)
    else:
        fields = re.split(r"\s+", line, maxsplit=1)
        code.append((fields[0], fields[1].split(",") if len(fields) == 2 else []))

PTEH, ASSOC, BL = 0xff000000, 0xf6000080, 0x10000000
MASK = 0xffffffff


def run(vpn, requested, active, size, shared, itlb_only, saved_sr):
    regs = {f"r{i}": 0xa5a50000 + i for i in range(16)}
    regs.update(r4=vpn, r5=requested)
    saved_regs = regs.copy()
    pteh = saved_pteh = 0x07654000 | active
    sr, pc, p2, writes, last_array = saved_sr, 0, False, 0, None
    target = requested & 255
    base = vpn & ~(size - 1)
    # Shared entries cannot coexist with a same-VPN private match.
    peer_vpn = base + size if shared else base
    peer_asid = (target + 1) & 255
    entries = [dict(vpn=base, asid=target, shared=shared, size=size, valid=True),
               dict(vpn=peer_vpn, asid=peer_asid, shared=False, size=size, valid=True),
               dict(vpn=base + 2 * size, asid=target, shared=False, size=size, valid=True)]
    utlb = [] if itlb_only else [e.copy() for e in entries]
    itlb = [e.copy() for e in entries]
    for step in range(100):
        op, args = code[pc]
        args = [a.strip() for a in args]
        pc += 1
        if op == "nop":
            pass
        elif op == "jmp":
            assert regs[args[0][1:]] == 0xac010040, "must enter P2"
            assert code[pc] == ("nop", []), "unexpected delay slot"
            p2 = True
            pc = labels["mmu_invalidate_tlb_real"]
        elif op == "rts":
            assert code[pc] == ("nop", []), "unexpected return delay slot"
            assert last_array is not None and step - last_array >= 8
            assert pteh == saved_pteh and sr == saved_sr, "caller state changed"
            assert writes == 1
            assert all(regs[f"r{i}"] == saved_regs[f"r{i}"] for i in range(8, 16))
            for array in (utlb, itlb):
                assert [e["valid"] for e in array] == ([False, True, True] if array else []), (
                    requested, active, size, shared, itlb_only, array)
            return
        elif op == "stc":
            assert args[0] == "sr"
            regs[args[1]] = sr
        elif op == "ldc":
            assert args[1] == "sr"
            sr = regs[args[0]]
        elif op == "mov.l":
            src, dst = args
            if src.startswith("@"):
                assert regs[src[1:]] == PTEH
                regs[dst] = pteh
            elif dst.startswith("@"):
                address, value = regs[dst[1:]], regs[src]
                assert p2, "MMIO before P2 entry"
                if address == PTEH:
                    assert sr & BL, "ASID change without exception exclusion"
                    pteh = value
                else:
                    assert address == ASSOC
                    assert sr & BL, "array write without exception exclusion"
                    assert value & 0x300 == 0, "D/V must be cleared"
                    for array in (utlb, itlb):
                        matches = [e for e in array if e["valid"] and
                                   value & ~(e["size"] - 1) == e["vpn"] and
                                   (e["shared"] or e["asid"] == pteh & 255)]
                        assert len(matches) <= 1, "multiple hit"
                        for entry in matches:
                            entry["valid"] = False
                    writes += 1
                    last_array = step
            else:
                regs[dst] = constants[src]
        elif op in ("or", "and", "extu.b"):
            lhs, dst = regs[args[0]], args[1]
            regs[dst] = ((lhs | regs[dst]) if op == "or" else
                         (lhs & regs[dst]) if op == "and" else lhs & 255) & MASK
        else:
            raise AssertionError(f"unsupported instruction {op}")
    raise AssertionError("did not return")


rng = random.Random(0x41534944)
count = 0
for target in range(256):
    for size in (1024, 4096, 65536, 1048576):
        for shared, itlb_only in ((False, False), (False, True), (True, False)):
            for active in (target, (target + 1) & 255):
                vpn = 0x10000000 + rng.randrange(size)
                # Include high argument bits: only the low eight select ASID.
                run(vpn, target | (0xdead0000 if target & 1 else 0), active,
                    size, shared, itlb_only, 0x40000001 | (BL if target & 2 else 0))
                count += 1
print(f"MMU-TLB-ASM: PASS cases={count} (register/MMIO model only)")
