# Shared timer-event probe

This probe verifies that timer-event objects do not create their shared carrier
thread until first use, that multiple timers share exactly one carrier, and
that destroying the final attached timer releases it. It also exercises:

- one-shot self-rearming;
- fixed-rate periodic callbacks and self-cancellation;
- rejection of destruction from the timer's own callback;
- cancellation before a distant deadline;
- coherent expiration counters;
- destruction after callback completion.

The program prints `TIMER-EVENT: PASS` and exits when all checks pass.
