# Bounded memory-backed sound effects

This example loads four complete WAV files with `fs_load()`, retains the byte
count returned for each allocation, and passes both pointer and size to
`snd_sfx_load_wav_buf()`. The bounded entry point validates RIFF chunk traversal
and sample data before copying anything to sound RAM.

The controller face buttons play separate effects. The directional buttons
reuse channel zero, the triggers adjust volume, and Start exits.
