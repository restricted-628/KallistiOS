Build with the checkout's Dreamcast environment and run the ELF in Flycast.
The fixture starts a private 44.1 kHz stereo stream with two source generations.
The first includes zero-valued PCM and a gap. The second continues into the
next transfer and ends partway through it. The checks require both sealed source
counts to finish, the output cursor to cross a ring wrap, and close to refuse
reuse after software stop.

The program prints `RETAINED PCM PASS` and stays alive with the lease retained.
The marker covers playback position accounting in the guest. Audible end and
physical final-fetch retirement require separate hardware evidence.
