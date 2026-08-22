# Asynchronous CD request-engine validation

This program deliberately combines several advanced request-engine behaviors:
queue ordering, cancellation, callback dispatch, waiting for another request
from callback context, typed CDDA status, DMA completion, and media events. It
is an integration test rather than the smallest possible asynchronous-read
example.

The cleanup helper is the important application pattern. It uses a finite wait;
on failure it requests cancellation and drains the request; it waits for any
queued callback; only then does it destroy the request. Partial submission is
handled with the same sequence so callbacks cannot retain stack-owned output.
