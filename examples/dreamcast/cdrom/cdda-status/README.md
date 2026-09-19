# CDDA status validation

This example validates KOS CDDA controls and typed playback status.
It requires a mixed-mode image with an audio track at track 2. The automated
portion exercises track and FAD-range playback, synchronous and asynchronous
status, pause, resume, stop, and the relationship between track-relative time
and absolute FAD.

After the CDDA checks, the program offers an optional live media-swap test.
Eject and reinsert the image through the emulator menu to validate media-event
delivery and cached drive-state transitions. Letting either prompt expire skips
that interactive portion.

When packaging this example as a multi-track emulator image, store the KOS
binary in the ISO image **unscrambled**. This packaging rule is specific to the
test layout and is not a KOS runtime requirement or a media-support claim.

Playback position and control behavior still require final confirmation on a
physical Dreamcast and CD-R before they are treated as hardware-validated.
