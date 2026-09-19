# Exclusive TMU1 channel

This example validates the opt-in high-resolution TMU1 API. It claims the
channel, configures a one-millisecond periodic interrupt, stops it from its own
callback, and releases the claim. It also verifies that a second owner and the
legacy mutating API cannot disturb an active claim, then checks that legacy use
works again after release.

The callback executes in interrupt context and therefore only updates bounded
state and stops its own channel. The main thread performs all reporting and
resource release.

The program prints `TMU1-CHANNEL: PASS` and exits when all checks pass.
