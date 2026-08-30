#!/bin/sh

# KallistiOS ##version##
#
# Run the self-contained host-test suites under a selected language policy.
# Copyright (C) 2026 Joseph Black

set -u

mode=${1:-gnu17}
compiler=${2:-${CC:-cc}}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

case "$mode" in
    gnu17)
        standard=gnu17
        pedantic=
        lz4_warnings=
        ;;
    c23)
        if "$compiler" -std=c23 -x c -fsyntax-only - </dev/null \
                >/dev/null 2>&1; then
            standard=c23
            lz4_warnings=
        elif "$compiler" -std=c2x -x c -fsyntax-only - </dev/null \
                >/dev/null 2>&1; then
            # Clang versions predating the final option spelling call the
            # same draft language mode C2x.
            standard=c2x
            # Older Clang diagnoses intentional constant-folding expressions
            # in the bundled LZ4 implementation. Keep that exception scoped
            # to tests which compile LZ4 rather than weakening the full lane.
            lz4_warnings=-Wno-constant-logical-operand
        else
            echo "$compiler does not support C23 or C2x" >&2
            exit 2
        fi
        pedantic=-pedantic
        ;;
    *)
        echo "usage: $0 [gnu17|c23] [compiler]" >&2
        exit 2
        ;;
esac

makefiles=$(find "$script_dir" -mindepth 2 -maxdepth 2 -name Makefile \
    -exec grep -l 'Makefile.host-test' {} \; | sort)

if [ -z "$makefiles" ]; then
    echo "no host-test Makefiles found" >&2
    exit 2
fi

total=0
passed=0
failed=0

echo "host tests: compiler=$compiler standard=$standard pedantic=${pedantic:-no}"

for makefile in $makefiles; do
    test_dir=$(dirname -- "$makefile")
    test_name=$(basename -- "$test_dir")
    total=$((total + 1))

    printf '%-34s' "$test_name"
    make -C "$test_dir" clean >/dev/null 2>&1 || true
    if make -C "$test_dir" CC="$compiler" HOST_CSTD="$standard" \
            HOST_PEDANTIC="$pedantic" \
            HOST_LZ4_WARNINGS="$lz4_warnings" test >/dev/null; then
        echo "PASS"
        passed=$((passed + 1))
    else
        echo "FAIL"
        failed=$((failed + 1))
    fi
    make -C "$test_dir" clean >/dev/null 2>&1 || true
done

echo "total=$total pass=$passed fail=$failed"
test "$failed" -eq 0
