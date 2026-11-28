# Rain passing under a shelter

A world-space prism describes the area under an eave. Its lower bounds are
inclusive and its upper bounds are exclusive. Wind supplies horizontal
acceleration to active drops. After the ordinary particle step, the scene
sets the hidden flag on drops inside the prism. A drop leaving cover resumes
drawing at its current position. Lifetime continues to run under cover.

With the KOS environment active, build and check the example with
`gmake -C examples/dreamcast/pvr/rain_shelter host-test all`. The host compares
a covered trajectory with an unobstructed reference and an independent
acceleration calculation. It also replays the same variable time steps and
wind inputs and checks expiry while hidden.

Run `rain-shelter.elf` with RTT readback enabled in Flycast. On startup the
guest renders an open witness drop beside the moving drop. It toggles table
fog, and removes the shelter at the same simulation instant as a control.
The clear and fogged coverage must agree at every pixel. After the first
screen render, `SHELTERED RAIN RTT PASS` appears on the serial console.
The scene then displays successive showers with a gray eave and blue fog.
