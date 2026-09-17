# Host UV mapping regression

`make test` exercises the host compiler's actual UV mapping code. It does not
mock the transform or invert a matrix to construct its expected goldens.

Coverage includes selected attribute identity, rotation/offset/nonuniform
scale, reflection, V-flip order, negative/repeating coordinates, in-place
evaluation, relative-map coefficients, all base/auxiliary V-flip combinations,
singular and ill-conditioned bases, different source sets, overflow, nonfinite
inputs and unchanged output on failure. A separate case demonstrates why
algebraic invertibility does not prove that quantized base UVs can reconstruct
an auxiliary layer. Storage-selection tests now enforce that distinction:
signed UV8/UV10 quantization, a seam only in the last corner, exact error-budget
boundaries, different sets with coincident values, relative-map overflow,
identity rows for independently baked data and late malformed inputs. Literal
affine equations provide an independent error oracle over 65 corners in each
signed precision mode. Selection outputs cannot alias either transform or the
sample array and remain unchanged on failure.

The normal host runner discovers this Makefile for GNU17 and strict C23 lanes.
The converter's Python suite independently decodes emitted signed-UV strip
references for transformed, flipped, reflected and collapsed glTF fixtures,
and checks that a missing selected UV set preserves the output file.

The companion `pvr-chunk-uv-asset-test` exercises selection through the actual
PML1/PUV1 codecs and runtime renderer, including a collapsed base mapping,
independently baked coordinates, seams and reversed strips. This suite alone
is host-tool behavior, not a completed auxiliary material importer, PVR image
test or physical-hardware result.
