# VMU filesystem image validator

`vmufs-image-test` applies the same metadata validator exported by KOS to a
raw VMU card image. It does not modify the image.

Build and run the synthetic regression suite:

```sh
gmake test
```

Inspect a standard card image whose root is at block 255:

```sh
./vmufs-image-test card.bin
```

An alternate root block may be selected explicitly:

```sh
./vmufs-image-test --root-block 255 card.bin
```

The tool checks root geometry, directory entries, exact FAT-chain lengths,
cycles, cross-links, duplicate names, allocated orphan blocks, total free
space, and the executable-eligible free prefix beginning at block zero.

The synthetic suite also exercises high-to-low data allocation, contiguous
block-zero executable allocation, all-or-nothing allocation failure, canonical
standard-card format construction, and every metadata commit prefix used by
copy-on-write replacement and deletion. It generates 128 randomized fragmented
filesystems and proves that the defragment plan preserves every logical block,
keeps an executable at block zero, retains total free space, produces one
packed layout, and is idempotent on a second pass.
