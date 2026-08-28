# Prepared compact-model wireframe

This example builds a four-reference triangle-strip cache and draws its
wireframe through constant-width screen-space quads. It cycles among the full
mesh, outside boundary, and consecutive-reference path policies so their
topology is directly visible.

The model contains no wireframe command. Cache storage, resolved-strip scratch,
profile, material header, scene, and list remain application-owned. The example
performs no allocation in its frame loop.
