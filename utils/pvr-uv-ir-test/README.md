# Host UV mapping regression

`make test` exercises the host compiler's actual UV mapping code. It does not
mock the transform or invert a matrix to construct its expected goldens.

Coverage includes selected attribute identity, rotation/offset/nonuniform
scale, reflection, V-flip order, negative/repeating coordinates, in-place
evaluation, relative-map coefficients, all base/auxiliary V-flip combinations,
singular and ill-conditioned bases, different source sets, overflow, nonfinite
inputs and unchanged output on failure. A separate case demonstrates why
algebraic invertibility does not prove that quantized base UVs can reconstruct
an auxiliary layer.

The normal host runner discovers this Makefile for GNU17 and strict C23 lanes.
The converter's Python suite independently decodes emitted signed-UV strip
references for transformed, flipped, reflected and collapsed glTF fixtures,
and checks that a missing selected UV set preserves the output file.

This is host-tool behavior only. It is not a runtime UV attribute stream,
auxiliary material importer, PVR image test or physical-hardware result.
