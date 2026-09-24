# LZ4 adapter regression tests

`make test` builds the unchanged upstream library and production KOS decoder
with instrumented malloc/calloc/free. This core-only bundle excludes the
Service Executor wrapper and its job/scheduler tests. Those remain on the
SH4ZAM addon bundle. The common host-suite runner finds this Makefile automatically.

Coverage:

- State creation prepares both upstream scratch buffers without consuming
  frame payload or writing the destination.
- Every creation allocation can fail with ENOMEM, with no leaked allocations
  and no destination modification.
- Small-budget and multi-block decoding perform no further allocations;
  linked and independent frames preserve byte budgets, CRCs, and output guards.
- Dictionary-backed linked and independent multi-block frames survive setup
  priming (upstream must receive the dictionary before leaving its init stage).
- Header requirements match actual upstream scratch requests for all four
  advertised block sizes, in both independent and linked modes.
- One-byte-below limits reject before scratch allocation; exact limits pass.
  The Compact profile rejects linked/oversized blocks, while NULL/zero caps
  and legacy entry points retain general frame compatibility.
- Header query failures leave results untouched, distinguish ENOMEM from
  malformed headers, and leak nothing. State and job rejection leave output
  untouched even when the first scratch allocation is configured to fail.

The scheduler double observes calls to yield; it does not emulate real fiber
context switching, preemptive races, interrupt submission, or hardware latency.
The Dreamcast `pvr/chunk_asset` example exercises the real service runtime.

Examples:

```sh
make clean test CC=clang
make clean test CC=gcc-14 HOST_CSTD=c23 HOST_PEDANTIC=-pedantic
make clean test CC=clang HOST_CSTD=c2x HOST_PEDANTIC=-pedantic HOST_LZ4_WARNINGS=-Wno-constant-logical-operand
make clean test CC=clang CFLAGS='-O1 -g -std=gnu17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Do not run different compiler lanes concurrently in this directory.
The strict Clang lane uses the host runner's existing suppression for constant
logical operands in unchanged upstream LZ4; it does not require vendor edits.
