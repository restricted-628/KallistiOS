# PVR render tickets

This example alternates an off-screen texture render with a displayed
framebuffer render. Each scene returns an immutable identity ticket. The test
waits for the texture ticket's hardware completion and the framebuffer
ticket's VBlank display separately, verifies that texture renders reject the
display stage, and checks the pipeline identity snapshot.

It prints `RESULT: PASS (PVR render tickets)` after 60 render pairs.
