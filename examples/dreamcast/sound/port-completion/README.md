# Experimental current-playback completion

Build with the normal ARM and SH-4 toolchains. This example needs optional firmware
feature0x20. It exercises short PCM16 completion before host polling, two selected
channels, fresh end-based priority admission, loop exclusion, unread-loop
replacement, UPDATE/ACK retention, channel reuse and deliberate legacy interference.

The experiment retains its original resident samples for its entire lifetime.
END_SEEN is an experimental LP observation, not physical qualification of
old/new-playback attribution, selector timing, release-tail completion or safe
sample-memory reuse. No hardware/performance/audio-quality claim follows from the
console marker. Explicit STOP still precedes software detach; no global reset or
sample reuse test is performed. Middleware remains outside this example.
