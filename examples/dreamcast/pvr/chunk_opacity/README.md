# Compact-model opacity routing

This example compiles one glTF mesh containing opaque, masked, and blended
materials into one compact model. The runtime admits and prepares that model
once, builds an ordinary decoded cache, and admits it with
`pvr_chunk_model_cache_draw_prepare()`. Each frame uses the cached
material-binding filter to submit one strip to each corresponding PVR polygon
list, without decoding the model streams or rescanning immutable cache data.

The red panel is opaque. The green panel uses per-vertex alpha and the global
0.5 punch-through threshold, producing a hard alpha boundary. The blue panel
uses authored half opacity and source-alpha blending. The source also marks
the masked and blended materials double-sided.

The example deliberately keeps scene and list ownership explicit. It checks
that every pass emits exactly one four-vertex strip, waits for completion, and
rejects any persistent PVR fault before reporting success.

Before rendering, each pass compares indexed-stream and admitted-cache output
in guarded memory sinks. Complete packets must be byte-identical, including
the authored alpha/color values and strip terminators. The serial marker is
`KOSOPACITY cached_parity=PASS passes=3 guards=PASS`. These one-time checks do not
submit material headers or acquire resources. Material/list checks stay active
in the render loop. Caller-owned cache storage is allocated once and freed
after the render drain and PVR shutdown; there is no per-frame allocation.
