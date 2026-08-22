# Camera SD image dump

This example downloads every reported stored image with a bounded deadline and
writes the results to an ext2 filesystem mounted at `/sd`.

It expects an SD adapter, an MBR first partition, and an ext2 filesystem. The
camera's stored-image unit must be available as unit 1. Live video is outside
the scope of this example.
