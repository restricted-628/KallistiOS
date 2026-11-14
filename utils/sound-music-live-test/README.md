# Live music adapter checks

`make test sanitize host-test host-sanitize` executes the production ARM copied
packet dispatcher, observer, MIDI/wire/bank/backend code and SH4 owner/client.
MMIO and host OS/port/bank-manager calls are controlled fakes. Existing registry
and port suites separately cover those managers' internal serialization.

The ARM suite checks nominal tick conversion/remainders/wrap/reset and the actual
wait predicate; fragmented1..64channel claims; atomic record preflight; all
claimed-channel command guards, mixed sync masks and retained observed rejection;
private START identity, conservative absent natural completion, no music LP read,
and latched faults blocking further hardware calls.

Eleven independent host processes cover unsupported firmware, partial reservation
and pin failure unwind, uncertain admitted/unadmitted CLAIM retries, changed host
or ARM session, withdrawn capability, bank OPEN pin transfer, terminal rejection,
unobserved OPEN followed by quiesce, failed unpin retention and a played-bank voice
lease retained after logical quiesce. Fresh processes model the intentionally
single persistent owner; there is no test-only production reset/free shortcut.

Normal and sanitizer results are software evidence. They do not establish real
G2 ordering, physical clock accuracy, audible output or safe sample/channel reuse.
The original target example is `examples/dreamcast/sound/music-live`.
