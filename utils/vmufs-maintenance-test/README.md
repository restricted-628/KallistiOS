# VMU maintenance-model tests

This host-side suite verifies the image transformations used to plan standard
128 KiB VMU formatting and defragmentation. It does not access or modify a
physical card.

Run it with:

```sh
gmake test
```

The suite checks the canonical root and FAT image, option validation, malformed
input rejection without partial directory mutation, and 512 randomized
fragmented filesystems. Every randomized result is validated, compared against
the complete 512-byte contents of every logical file block, checked for the
intended executable/free-space layout, and planned a second time to prove
idempotence.

The planner describes a correct final image only. A future physical-card
operation must provide its own interruption-safe commit protocol; this model
must not be treated as authorization to overwrite live blocks in plan order.
