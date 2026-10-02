#!/usr/bin/env python3
"""Run bounded deterministic schedules; optional second argument saves JSON."""
import json
import subprocess
import sys

cases = ["metadata", "reset", "media", "bios-order", "readdir",
         "partial-interleave", "partial", "async", "direct-async",
         "close-callback", "cancel", "preseek", "stream", "stream-close",
         "prefetch-reset", "errors", "mount", "direct-bypass", "sync-busy",
         "retire-under-cache", "bios-stream", "legacy-close", "submit-failure"]
results = []
for case in cases:
    for backend in ("direct", "bios"):
        if (case in ("bios-order", "bios-stream", "legacy-close") and backend == "direct") or (
            case in ("direct-bypass", "sync-busy") and backend == "bios"
        ):
            continue
        command = [sys.argv[1], case, backend]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=10)
            row = dict(case=case, backend=backend, exit=result.returncode,
                       stdout=result.stdout, stderr=result.stderr)
        except subprocess.TimeoutExpired:
            row = dict(case=case, backend=backend, exit=124, stderr="timeout")
        results.append(row)
        print(f"{case} {backend}: {'PASS' if row['exit'] == 0 else 'FAIL'}")
if len(sys.argv) > 2:
    with open(sys.argv[2], "w") as output:
        json.dump(results, output, indent=2)
print(f"{sum(r['exit'] == 0 for r in results)}/{len(results)} passed")
sys.exit(any(row['exit'] for row in results))
