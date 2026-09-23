# Core fiber owner teardown probe

Runs 32 joined owner-thread lifetimes: eight repetitions of default/math-enabled
attachment, each exiting either by returning from the owner function or calling
`thd_exit()` inside a child fiber. The owner leaves an undispatched child, a
suspended child, and a finished child (or the child which exits the whole thread).
No synchronization objects or held locks remain at exit.

Test-only linker wrappers account for `calloc` and `aligned_alloc` during the
owner's attachment/creation calls and observe corresponding `free` calls during
join. Each case must reclaim its runtime, three child objects, and, when enabled,
four XMTRX buffers. This intentionally checks the current allocation layout, not
a promise of public ABI. Process-wide TLS-key setup, TLS-entry allocations and
the rest of the kernel allocator are outside this scoped accounting.

Borrowed stacks are never freed and are immediately overwritten/reused after
join. Suspended application code must not be resumed as part of destruction.
Assertions must remain enabled. Success prints:

`KOSFIBERTEARDOWN cases=32 reclaimed=192 borrowed-frees=0`

This does not cover detached-thread reaping, forced thread destruction, live
cooperative objects at exit (forbidden by their contract), or private executor
callbacks. It does not establish physical-hardware or MMU-on correctness.
