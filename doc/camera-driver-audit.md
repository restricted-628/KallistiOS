# Camera driver audit

## Scope

This audit covers the Maple camera driver's stored-image command path. It
classifies live video separately because no verified live-capture transport is
present in the current KOS driver or in the protocol material available to this
tree. The implementation therefore does not guess at live-video commands or
advertise unverified capability.

## Baseline defects

The original stored-image implementation had several coupled lifetime and
protocol problems:

- one process-global pointer selected the destination of every transfer, so
  simultaneous cameras and late replies could target the wrong state;
- image reads waited forever and assumed camera units 2 through 5 existed;
- response callbacks asserted on peripheral input from IRQ context;
- malformed count and erase replies could leave blocking callers reporting
  success or waiting until timeout without a useful result;
- response chunk numbers and lengths were trusted before copying into the
  allocated image buffer;
- initial frame-lock and queue failures were ignored; and
- detach or timeout could release state while a SENT Maple frame still owned a
  response buffer that would later reach the callback.

## Implemented KOS behavior

The driver now owns one stored-image transfer context per physical Maple port.
Transfers on different ports are independent, while a second operation on the
same camera port returns `MAPLE_EAGAIN`. The legacy `dreameye_state_t` remains
the first member of the private status allocation, preserving existing
`maple_dev_status()` use.

New public facilities are:

- `dreameye_get_image_timed()` for a bounded complete image read;
- `dreameye_get_image_transfer_count()` for validated transfer geometry; and
- `dreameye_get_status()` for a coherent operation, response, progress, error,
  and diagnostic snapshot.

`dreameye_get_image()` remains available and uses the documented default
deadline. The existing count and erase functions keep their `block` behavior;
nonblocking submission can now be observed through the status snapshot.

## Transfer invariants

The stored-image engine enforces the following rules:

1. Transfer geometry must contain 1 through 256 chunks. The eight-bit chunk
   index can therefore address every accepted chunk without wrapping.
2. Only the camera units needed by the transfer are required, and every one is
   validated before submission.
3. Up to five unit-owned Maple frames stripe chunks across the port. A frame is
   reused only after its callback consumes the previous response and unlocks
   it.
4. Every response validates its command shape, function word, chunk index,
   expected lane index, payload size, destination bound, and duplicate status
   before copying.
5. Completion requires the terminal marker, every expected unique chunk, and
   zero outstanding frames. Out-of-order lane completion does not suppress
   remaining lower-numbered chunks.
6. A timeout stops new work and repeatedly removes only UNSENT frames. SENT
   frames retain the transfer allocation until their IRQ callbacks drain.
7. If a late hardware response outlives the bounded drain period, the driver
   quarantines the allocation and reaps it only after all callbacks have made
   the transfer terminal. This trades a bounded temporary allocation for
   freedom from late-response use-after-free.
8. Device removal marks the transfer disconnected, cancels removable frames,
   and never dereferences the driver's freed per-device status from a late
   callback.

No thread, worker stack, periodic poll, or image buffer is allocated merely by
initializing the camera driver. Image storage is allocated only for a requested
stored-image transfer and is sized from validated geometry.

## Validation record

- The Dreamcast cross-build succeeds with GCC 16.2.0.
- The driver passes `-Wall -Wextra -Werror -fanalyzer`.
- Both stored-image examples compile and link against the rebuilt KOS archive.
- Dynamic-module export generation includes the legacy and new public camera
  operations.

## Remaining physical gates

The following require an attached camera and are not claimed by software-only
validation:

- real response flag and short-final-chunk behavior;
- multi-unit ordering and maximum stored-image geometry;
- detach and reconnect during each transfer phase;
- timeout behavior under actual peripheral latency; and
- any live-video acquisition path.

Live video remains explicitly deferred until its command transport, frame
format, timing, and buffer ownership can be established from reproducible
behavior. It is not necessary to weaken the now-bounded stored-image path to
prepare for that future facility.
