# Stream splitter and lifecycle tests

Run `make test` with Python 3 and a sanitizer-capable host C compiler available.
The ordinary C fixture tests exact PCM/ADPCM splitting, including short tails.
The Python runner compiles exact source slices for checked stream stop/destroy
and service destroy, with deterministic external dependencies and reduced types.
It does not duplicate the production stop/destroy control flow.

Eleven cases check ownership/refusal, callback reentry, command admission,
FIFO barrier, DMA/status/playing failure, ordered success, and service self-call,
timeout and join-before-free. Every failed destroy must retain its allocation.
One additional case intentionally supplies playing=false with fetch_live=true
and verifies the current destroy path releases storage. This is a known missing
guarantee in the model; it does not reproduce continued hardware sample fetching
or choose a release delay. PCM data tails and an AICA envelope release tail are
different questions.

HOST_CC and HOST_CFLAGS control the lifetime compiler/options; defaults use
GNU17, warnings-as-errors and ASan/UBSan. STREAM_LIFETIME_BUILD_DIR selects the
generated source/binary/manifest directory (default `.lifetime-build`, ignored).
The source slices and their original input hashes are recorded in manifest.json.
This runner does not execute the complete stream service, firmware, DMA, VFS,
IRQ or cache implementation. Hardware sample retirement remains an open gate.
