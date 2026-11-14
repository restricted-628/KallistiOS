# Explicit ARM firmware layout check

After sourcing the checkout environment, run:

```
make -C kernel/arch/dreamcast/sound/arm check-layout
```

This requires Python 3 and the selected ARM compiler/binutils. It is an explicit
integration validation for firmware changes; ordinary builds do not invoke it
and do not gain a Python dependency. It does not universally prevent unchecked
future builds from growing into the stack. The prebuilt fallback is not checked
or refreshed by this target; an ARM compiler and freshly built objects are required.

The checker compares the ELF LOAD image with the raw firmware, checks startup SP
and undefined symbols, recompiles every linked C unit with static stack records,
and compares allocated sections and resolved relocations with the original objects.
Relocations include their target section, offset, type, addend and symbol identity
(name, binding/type, visibility, definition section, value and size); equal numeric
symbol-table indexes alone are insufficient. `make check-layout-test` in the same
ARM directory builds a negative fixture with equal code/raw relocations but
different external call targets, which the comparison must reject. The checker
sums all C frames plus a conservative 512-byte helper reserve, and checks that
image/BSS plus that bound stays below SP. ARM compilation of the state headers
checks the actual private aggregates and receipt boundaries. Unsupported ISA
families and changed helper implementations fail the check.

`helpers.json` fingerprints the inspected ARM7DI libgcc implementations. The
new unsigned division chain uses 64 bytes: wrapper 16, division 40, leading-zero
wrapper 8, and a leaf byte-table implementation. The 512-byte reserve also covers
previously reviewed helpers. A different compiler or helper must be inspected
before updating fingerprints; hashes are identity checks, not proof by themselves.

This is a conservative static bound for the current nonrecursive, serialized C
call graph. The FIQ uses banked registers without stack/C calls. New recursion,
reentrant callbacks, interrupt-stack use or assembly requires fresh call-chain
review; summing C frames alone would not cover it. This does not measure stack
high-water, scheduling deadlines, audible behavior or physical sample retirement.
