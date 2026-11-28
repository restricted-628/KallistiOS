# SH-4 video IDCT

Falco Girgis wrote the original SH4ZAM inverse discrete cosine transform
and supplied it directly to Joseph Black for modification. Joseph Black
adapted the transform for libSFD's natural-order float coefficients and
rounded residual output.

- [Production kernel](src/idct.c): dense blocks use a matrix for the odd
  terms and scalar arithmetic for the even terms. Zero and DC-only blocks
  take exact shortcuts.
- [Two-matrix kernel](src/idct_dual.c): the fixture build includes this
  adaptation of the supplied implementation for comparison. It is not the
  decoder default or a runtime fallback.
