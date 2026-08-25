# Compact-model texture resources

This example binds the texture identifier stored in a compact model to an
application-owned `pvr_txr_surface_t` without creating a global asset registry.
The sorted table is validated once, then its admitted view is shared with a
caller-owned material submission adapter.

For every strip, the established compact renderer:

1. decodes persistent model state;
2. resolves texture identifier 7 through the caller's table;
3. compiles and submits an ordinary checked KOS polygon material; and
4. projects and emits the strip through the current PVR list sink.

The application still owns texture allocation, upload, model data, the scene,
the list, and all object lifetimes. The example renders for 120 frames, checks
the PVR fault record, and displays a green pass marker.
