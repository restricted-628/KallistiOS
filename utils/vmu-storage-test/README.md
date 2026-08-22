# VMU storage safety tests

This host test exercises exact, bounded FAT-chain resolution and the VMU
package codec without requiring a console or memory card.

Run it with:

```sh
gmake test
```

The cases cover valid and corrupt chains, cycles, out-of-range blocks,
premature and overlong chains, undersized caller storage, package round trips,
unaligned encoded input, bounded text fields, truncated layouts, invalid icon
metadata, and checksum failures. Package parsing is also checked to leave its
input byte-for-byte unchanged.
