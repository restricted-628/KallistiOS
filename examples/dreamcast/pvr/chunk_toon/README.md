# Compact-model topology-aware band shading

This example builds an ordinary prepared compact-model draw cache, then emits
it through the allocation-free band-shading policy. The source triangle has
smooth normals on opposite sides of one threshold. Its moving directional
light therefore creates a hard color boundary that crosses the triangle
interior instead of merely changing the three original vertex colors.

Before the band pass, the example emits the same prepared cache as an expanded
dark shell. This single open triangle deliberately keeps culling disabled so
both sides demonstrate the geometry. A closed model should submit an outline
header with the ordinary surface pass's opposite culling mode, leaving only
the enlarged back faces visible around the final surface.

The model contains no renderer-specific record. A caller-owned profile selects
the scalar equation, threshold, and two packed color modulations. Caller-owned
work arrays retain the resolved positions, transformed normals, scalar shades,
at most three band triangles, and the established frustum-clipping workspace.
The prepared cache, material header, scene, list, and all memory remain under
application control; no per-frame allocation or global matrix state is used.
