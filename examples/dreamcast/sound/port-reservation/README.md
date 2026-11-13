# Cooperative sound ports

This probe reserves mono/pair groups alongside legacy channel allocations,
checks exhaustion rollback and stale handles, then returns every reservation.
It does not initialize sound, play samples or test hardware retirement.

`dc/sound/port.h` provides reserve/query/close. Pair channels need not be
adjacent. Raw channel users must cooperate; earlier automatic SFX can still be
playing on newly reserved channels. Close only returns software ownership and
never establishes that a sample may be freed. Handle generations never wrap;
exhausted slots remain unavailable until runtime restart.

Build with the checkout's environment and `make`; run with `make run`.
The host fixture in `utils/sound-port-test` additionally exercises private
transactional priority admission, including failed submission and exhaustion.
