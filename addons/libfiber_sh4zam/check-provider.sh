#!/bin/bash
# Read-only link audit. Run after `make probes`.
set -euo pipefail
cd "$(dirname "$0")"
task_nm="${SH4_NM:-${KOS_CC_BASE:?Set KOS_CC_BASE or SH4_NM}/bin/${KOS_CC_PREFIX:-sh-elf}-nm}"

task_undefined="$("$task_nm" -u .build/libfiber_sh4zam.a)"
if rg ' U _(mat_|vec_|sinf$|cosf$|sqrtf$|powf$)' <<< "$task_undefined"; then
    echo 'FAIL: addon imports KOS/libm math' >&2
    exit 1
fi
if rg -n '#include[[:space:]]*<(dc/(matrix|vector)\.h|math\.h)>|\bmat_(load|store|trans)' src; then
    echo 'FAIL: addon source uses a KOS math backend' >&2
    exit 1
fi
for task_probe in fiber-context-probe fiber fiber-sync fiber-math fiber-teardown \
                  fiber-service-probe fiber-service-sync fiber-service-queue; do
    task_map=".build/$task_probe.elf.map"
    test -s "$task_map"
    if rg 'libkallisti\.a\([^)]*fiber[^)]*\.o\)' "$task_map"; then
        echo "FAIL: $task_probe mixes in a kernel fiber implementation" >&2
        exit 1
    fi
    for task_member in fiber fiber_sync fiber_service fiber_context fiber_switch; do
        rg -q "libfiber_sh4zam\.a\($task_member\.o\)" "$task_map"
    done
done
echo 'PASS: 8 link maps use only the addon fiber provider; no KOS/libm math imports'
