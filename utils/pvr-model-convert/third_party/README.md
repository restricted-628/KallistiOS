# Host converter dependencies

`cgltf.h` is the unmodified cgltf 1.15 single-header glTF 2.0 parser from
upstream commit `85cd62382dfea638278962690cf515023f33ed00`.

- License: MIT, reproduced in the header.
- SHA-256: `efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9`
- Scope: host-side `pvr-model-convert` only; it is not linked into KOS or a
  Dreamcast program.

Keep this file pristine when updating it. Record the new revision and checksum
here, then rerun both strict host-language lanes and the converter fixtures.
