# Compact PVR Model Inspector

`pvr-model-inspect` validates the two bounded streams consumed by KOS's
compact-model runtime and prints deterministic structural statistics. It links
the runtime parser directly, so the host check and target admission rules do
not drift apart.

The input files are raw little-endian streams:

- the vertex file contains 32-bit words;
- the polygon file contains 16-bit words; and
- both files include their terminating end record.

No container header is implied. File sizes supply the stream bounds, while the
optional center and radius arguments supply model metadata:

```text
pvr-model-inspect [--center X Y Z] [--radius R] [--] VERTICES POLYGONS
```

The default center is `(0, 0, 0)` and the default radius is zero. Successful
validation prints `key=value` records and exits with status 0. A structurally
invalid model exits with status 1. Command-line, file, and allocation failures
exit with status 2.

This utility intentionally does not define a second packaged-model format or
convert source geometry. A later converter can emit these same bounded streams
and use this executable as its independent admission check.
