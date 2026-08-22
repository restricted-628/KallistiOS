# VMU filesystem metadata validator tests

This host-side test exercises the pure VMU filesystem validator without a
Dreamcast or memory card. It covers root-block overlap, unsupported geometry,
malformed file types, empty files, out-of-range and prematurely terminated
chains, overlong chains, cycles, cross-links, duplicate names, orphan blocks,
buffer bounds, and executable-eligible free space.

Run `gmake -C utils/vmufs-validate-test test` from an initialized KOS source
tree. The test uses the host C compiler and does not read or modify a card
image.
