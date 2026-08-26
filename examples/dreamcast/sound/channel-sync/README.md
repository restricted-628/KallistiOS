# Synchronized AICA channel start

This example reserves AICA channels 31 and 32, stages the same looping PCM
waveform on both channels without starting either one, and then starts both
with one 64-channel synchronization command. Using channels on opposite sides
of the 32-bit boundary verifies both halves of the public channel mask.

The generated tone is panned hard left and right. It plays for two seconds,
then the example stops the channels and releases all sound-RAM and channel
resources. No external audio asset is required.
