"""Check GCC SH-4 codegen: bounded OCB loops must not repeat alias masking."""
# Copyright (C) 2026 Joseph Black
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
checked = 0
for function, opcode in (("range_invalidate", "ocbi"), ("range_writeback", "ocbwb"),
                         ("range_purge", "ocbp")):
    body = source.split("_" + function + ":", 1)[1]
    body = re.split(r"\s*\.size\s+_" + function + r"\b", body, maxsplit=1)[0]
    code, labels = [], {}
    for line in body.splitlines():
        line = line.split("!", 1)[0].strip()
        if re.fullmatch(r"\.L\d+:", line):
            labels[line[:-1]] = len(code)
        elif line and not line.startswith("."):
            fields = line.split(None, 1)
            code.append((fields[0], fields[1] if len(fields) > 1 else ""))
    edges = {}
    for pc, (op, target) in enumerate(code):
        delayed = op.endswith("/s") or op in ("bra", "bsr", "jsr")
        fallthrough = pc + 1 + int(delayed)
        if op in ("rts", "jmp"):
            destinations = []
        elif op == "bra":
            destinations = [labels[target]]
        elif op in ("bf", "bt", "bf/s", "bt/s"):
            destinations = [labels[target], fallthrough]
        else:
            destinations = [fallthrough]
        edges[pc] = {n for n in destinations if n < len(code)}
    reverse = {pc: set() for pc in edges}
    for pc, destinations in edges.items():
        for n in destinations:
            reverse[n].add(pc)
    def reachable(graph, start):
        seen, pending = set(), [start]
        while pending:
            pc = pending.pop()
            if pc not in seen:
                seen.add(pc)
                pending.extend(graph[pc] - seen)
        return seen
    loops = 0
    for pc, (op, _) in enumerate(code):
        if op != opcode:
            continue
        cycle = reachable(edges, pc) & reachable(reverse, pc)
        if len(cycle) == 1 and pc not in edges[pc]:
            continue  # A peeled first iteration is not a loop.
        inspected = set(cycle)
        for n in cycle:
            if code[n][0].endswith("/s") or code[n][0] == "bra":
                inspected.add(n + 1)  # Branch delay slot executes too.
        assert not any(code[n][0] in ("and", "or", "xor") for n in inspected), (
            function, "alias masking in OCB loop")
        loops += 1
    assert loops, (function, "no bounded OCB loop found")
    checked += 1
print(f"CACHE-CODEGEN: PASS functions={checked} alias-masks-in-loops=0 (GCC SH-4 codegen only)")
