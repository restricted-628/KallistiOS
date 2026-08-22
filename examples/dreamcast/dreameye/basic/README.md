# Camera stored-image example

This example locates camera unit 1, obtains a validated stored-image count, and
downloads the first image with a bounded deadline. On success it writes the
returned bytes to `/pc/image.jpg` and releases the driver-allocated buffer.

The example intentionally checks every transport and file result. It does not
demonstrate live video.
