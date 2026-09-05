# Compact scene integration test

Builds the real `chunk_scene` example's admission, pose, and prepared-emission
checks for the host with only hardware entry points replaced by aborting
stubs. The input is compiled from the example's authored glTF fixture using
the real host compiler. Expected poses are calculated independently of the
animation/deformation implementation.

`make test` uses the normal GNU17 policy. The shared `run-host-tests.sh`
discovers this suite for both the GNU17 and explicit strict-C23 lanes.
For a focused sanitizer run:

```sh
make clean
make test CC=clang CFLAGS='-O1 -g -std=gnu17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
```

The compiler performs its normal asset round-trip validation before the test
materializes both models, selects the named logical clip, samples six poses,
checks independent morph curves and skin palettes, and emits both prepared
caches into memory. No host stub is permitted to emulate a successful hardware
operation: reaching one aborts the process.

The runner also rejects a truncated container and a damaged second skin
payload. The latter fails after the first model's cache has been allocated,
exercising partial-load cleanup under the sanitizer lane.
