# Original resident sequence fixture

Rebuild KOS with the ARM7DI compiler and run the explicit firmware check-layout
target before linking. The unchanged prebuilt fallback lacks these features.
The local builders generate original ASQ programs and a triangle-wave tone bank.

The fixture checks play, paused position and retained CPU references, bounded
timed-note continue, looping/chaining into another resource, stop/all-stop,
direct-writer exclusion and CPU sequence allocation recovery. It retains the
played tone bank and 64-channel pool after logical quiesce. It establishes no
physical sample retirement, audible quality or shutdown result; PASS/FAIL leaves
the application alive with those retained objects.
