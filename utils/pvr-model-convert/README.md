# Compact PVR Model Converter

`pvr-model-convert` converts a deliberately bounded Wavefront OBJ subset into
the two little-endian streams consumed by KOS's compact-model runtime. The
generated model is admitted by the exact runtime validator before either
temporary output is published.

```text
pvr-model-convert [--flip-winding] [--flip-v] [--texture-id ID] [--] \
    INPUT.obj VERTICES.bin POLYGONS.bin
```

The admitted source subset is:

- finite `v X Y Z` positions;
- finite `vt U V` coordinates in `[0, 1]`;
- finite, nonzero `vn X Y Z` normals;
- triangulated faces using `v`, `v/vt`, `v//vn`, or `v/vt/vn`; and
- positive or relative-negative OBJ indices.

Every corner of a triangle must use the same attribute form. Object, group,
and smoothing declarations are accepted because they do not alter this
geometry stream. Material libraries and material selection are rejected rather
than silently discarded. Faces with more or fewer than three vertices are
also rejected; source triangulation policy stays in the content tool that owns
the original mesh.

UVs are quantized to unsigned 10-bit values. Normals are normalized and
quantized to signed 16-bit components. `--flip-v` applies `V = 1 - V`, while
`--flip-winding` exchanges the second and third corner of every triangle. No
coordinate-system transform is implicit. Models with UV-bearing faces require
one explicit 13-bit `--texture-id`; every face must then carry UVs. Material
selection remains application-owned rather than being inferred from names.

Arbitrary triangle topology is represented as three-vertex strips. Consecutive
triangles with the same attribute form share bounded strip records, and large
models are split across records before any 16-bit count can overflow. The tool
prints calculated center/radius metadata and stream sizes as deterministic
`key=value` records. Each output is replaced by an atomic rename after complete
write and validation; the two separate files cannot form one filesystem-atomic
transaction.
