# Disc convenience routing regression

This no-media probe executes the production `cdrom_*` wrappers with link-time
spies replacing direct transport, BIOS request submission, and BIOS firmware
calls. It checks direct-default seek and typed CDDA status, explicit BIOS
opt-in, argument/callback forwarding, submission failures without fallback,
BIOS Q-channel decoding, and direct error/sense translation to `ERR_*`.
Drive-status tests cover optional outputs and -1 failure sentinels, including
a decoded diagnostic payload followed by failure. TOC tests cover both density
areas and direct sense/error mapping. Explicit BIOS metadata calls are also
tested after selecting the direct filesystem backend.
It selects the BIOS filesystem backend first to ensure that raw convenience
calls remain independent of `/cd` policy.

The sentinel request handles are identities only and are never dereferenced.
The probe does not test request scheduling, live playback, DMA, timing,
physical-drive behavior, or hardware recovery. Run the separate request and
CDDA status integration examples with suitable media for those paths.
