# Compact-model opacity routing

This example compiles one glTF mesh containing opaque, masked, and blended
materials into one compact model. The runtime admits and prepares that model
once, then uses the standard material-binding filter to submit one strip to
each corresponding PVR polygon list.

The red panel is opaque. The green panel uses per-vertex alpha and the global
0.5 punch-through threshold, producing a hard alpha boundary. The blue panel
uses authored half opacity and source-alpha blending. The source also marks
the masked and blended materials double-sided.

The example deliberately keeps scene and list ownership explicit. It checks
that every pass emits exactly one four-vertex strip, waits for completion, and
rejects any persistent PVR fault before reporting success.
