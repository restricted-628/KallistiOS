# Caller-owned animation playback

This example admits two vector tracks, builds a two-node transform clip, and
advances an explicit looping playback cursor. Each sampled local-matrix array
feeds the compact-model hierarchy directly. The same cursor time also samples
a camera and an existing PVR point-light representation.

The example deliberately advances by a supplied `1/60` second step. The
animation layer creates no clock, thread, fiber, worker, or pose allocation;
an application may drive the same cursor from whichever scheduling model fits
its frame loop.
