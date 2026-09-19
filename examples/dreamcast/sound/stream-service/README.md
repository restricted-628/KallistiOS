# Optional sound-stream polling service

This example starts a generated stereo PCM stream and explicitly creates one
automatic polling service with a caller-owned 32 KiB stack. The service is not
part of ordinary sound initialization: applications that keep calling
`snd_stream_poll_ex()` themselves allocate no service object, thread, or stack.

While the stream is registered, the example verifies that a competing manual
poll is rejected. It then lets the service refill the ring buffer, checks
service and stream progress, removes the stream with a deadline, and performs
bounded teardown. No external audio asset is required.

The stack size is an application choice because the service invokes the
application's stream callback on that stack. A real program should budget for
its own decoder and callback depth rather than copying this example's value
without measurement.
