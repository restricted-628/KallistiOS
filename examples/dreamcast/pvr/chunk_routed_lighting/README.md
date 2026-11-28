# Compact routed lighting

This example renders a textured Compact receiver with separate directional
lighting on either side of a modifier volume. Its packed two-volume vertices
carry two base colors and two offset colors. The modifier switches between
the outside and inside material state without drawing the receiver twice.

Build with the tree's KOS environment active.

```sh
gmake -C examples/dreamcast/pvr/chunk_routed_lighting
gmake -C examples/dreamcast/pvr/chunk_routed_lighting host-test
```

Run `chunk-routed-lighting.elf` on the Dreamcast. For Flycast, select a
per-pixel renderer supporting two-volume materials and enable render-to-texture
readback. The Linux OpenGL per-pixel backend at revision 628bd3dbb supports
this fixture, including a texture target on the first frame. The guest renders four cases with zero to
three active lights and a hand-authored direct PVR control. It samples a
striped texture with mirrored U and checks inherited and overridden ambient.
Each draw starts with a magenta target. The guest waits for its render ticket,
checks the RGB565 colors on both sides of the modifier boundary and compares
the entire three-light frame with the direct submission. Success prints
`COMPACT ROUTED LIGHTING RTT PASS` to the serial console. The host test
checks signed accumulation, thin surfaces, both vertex layouts, preservation of
UVs and alpha, and rejection of an invalid update.

## Drawing with this policy

- Author the two-volume strip, its independent UVs, and its material masks
  on the development machine. The build runs the Compact planner and PCC1
  serializer on the host. The Dreamcast opens the resulting section,
  materializes native storage and admits it once for repeated draws.
- Call `sh4der_kos_routed_lighting_prepare()` when a light or the material routing changes.
  It compacts the active union of the masks and folds signed light intensity
  into the color coefficients. Each volume independently selects one mask for
  base RGB and one for additive offset RGB. Optional linear RGB888 ambient
  values supply the starting base and offset. Volume 1 inherits both outside
  values unless the caller selects its own pair. An explicit black override
  remains black when its light mask is empty.
- Supply `sh4der_kos_routed_lighting_vertex()` to the prepared two-volume draw. Its
  normals and directions must be unit length in the same coordinate space.
  Transform either before drawing when lighting and geometry use different
  spaces. It preserves both UV pairs and all alpha bytes. It replaces base RGB and
  both additive offset RGBs in textured packets. Color-only plans reject offset
  routes. Its callback computes just the two base colors. Pass the admitted
  cache format to preparation and draw with that same format.
- Use two-parameter modifier mode and enable offset colors in the textured
  material context when using offset routes. Keep texture resources live
  through render completion.

The example links the optional SH4DER addon. Its prepared lighting callback
uses the existing Compact renderer and packet formats.
