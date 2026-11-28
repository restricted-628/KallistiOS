# Compact cone spotlight

The sample moves a cone across the back wall of an open room. The development
machine cooks all five walls, strip references and hard normals into an
ordinary Compact cache section. The Dreamcast admits the section once. Each
frame prepares the changed lamp and uses the existing vertex callback before
projection. The lamp position, outward axis and resolved positions/normals
use the same coordinate space.

With the KOS environment active, run `gmake -C addons/libsh4der` and then
`gmake -C examples/dreamcast/pvr/chunk_spotlight`. Launch `chunk-spotlight.elf`.
Startup checks the moving and tilted cone, its falloff, short range and backface
on a 64×64 RGB565 surface. A rigidly transformed room and lamp must reproduce
the first frame byte for byte. Enable render-to-texture readback in Flycast.
The guest prints `COMPACT SPOTLIGHT ROOM PASS`, then displays the moving light
on screen. Run `gmake -C utils/sh4der-compact-test spotlight-test` for the
independent host color oracle.
