#!/bin/bash
# Build and audit the fiber integration bundle from this checkout only.
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd -P)"
test "$(cd "${KOS_BASE:?Source the KOS environment first}" && pwd -P)" = "$task_root" || {
    echo "FAIL: KOS_BASE points to a different checkout" >&2; exit 1;
}
task_kind="$(cat "$task_root/utils/fiber-bundle-provider")"
task_make="${KOS_MAKE:-gmake}"
task_nm="${KOS_CC_BASE:?}/bin/${KOS_CC_PREFIX:-sh-elf}-nm"
"$task_make" -C "$task_root" -j"${JOBS:-4}"
task_tests=(fiber-context-probe fiber fiber-sync fiber-math fiber-teardown)
if [ "$task_kind" = sh4zam ]; then
    task_tests+=(fiber-service-probe fiber-service-sync fiber-service-queue)
fi
for task_test in "${task_tests[@]}"; do
    "$task_make" -C "$task_root/examples/dreamcast/basic/threading/$task_test" -B CFLAGS=-Werror
done
for task_test in fiber-disc-contract fiber-read fiber-vram; do
    "$task_make" -C "$task_root/examples/dreamcast/cdrom/$task_test" -B CFLAGS=-Werror
done
task_kernel="$task_root/lib/dreamcast/libkallisti.a"
task_archive="$task_kernel"
if [ "$task_kind" = sh4zam ]; then
    task_archive="$task_root/addons/lib/dreamcast/libfiber_sh4zam.a"
    if "$task_nm" -g --defined-only "$task_kernel" | grep -E ' [TW] _(fiber_|arch_fiber_(context|math_context))'; then
        echo "FAIL: kernel contains a competing fiber provider" >&2; exit 1
    fi
    if "$task_nm" -u "$task_archive" | grep -E ' U _(mat_|vec_|sinf$|cosf$|sqrtf$|powf$)'; then
        echo "FAIL: addon imports KOS/libm math" >&2; exit 1
    fi
else
    if "$task_nm" -g --defined-only "$task_kernel" | grep -E ' [TW] _fiber_service_'; then
        echo "FAIL: core bundle contains the Service Executor" >&2; exit 1
    fi
fi
for task_symbol in fiber_attach_ex fiber_event_wait arch_fiber_context_switch arch_fiber_math_context_switch; do
    task_count="$("$task_nm" -g --defined-only "$task_archive" | grep -Ec " [TW] _${task_symbol}$")"
    test "$task_count" = 1 || { echo "FAIL: duplicate/missing $task_symbol" >&2; exit 1; }
done
for task_test in fiber-disc-contract fiber-read fiber-vram; do
    task_map="$task_root/examples/dreamcast/cdrom/$task_test/$task_test.elf.map"
    test -s "$task_map"
    if [ "$task_kind" = sh4zam ]; then
        if grep -Eq 'libkallisti\.a\(fiber[^)]*\.o\)' "$task_map"; then
            echo "FAIL: mixed provider in $task_test" >&2; exit 1
        fi
        task_lib=libfiber_sh4zam
    else
        if grep -q 'libfiber_sh4zam\.a' "$task_map"; then
            echo "FAIL: addon in core example $task_test" >&2; exit 1
        fi
        task_lib=libkallisti
    fi
    for task_member in fiber fiber_sync fiber_context fiber_switch; do
        grep -Fq "$task_lib.a($task_member.o)" "$task_map"
    done
done
echo "PASS: $task_kind bundle builds independently; disc examples link exactly one provider"
