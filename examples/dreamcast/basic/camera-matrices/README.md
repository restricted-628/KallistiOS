# Checked camera matrices

This example builds explicit perspective and look-at matrices without first
changing the SH-4 matrix register. It verifies that the checked builders match
the established KOS transform order, that `mat_compose()` has the same
post-multiply convention as `mat_apply()`, and that invalid input changes
neither caller-owned output nor the active matrix.

The descriptors retain no state and allocate no memory. Applications can
build, cache, compose, and validate camera matrices in ordinary storage before
choosing when to load or apply them. Existing `mat_perspective()` and
`mat_lookat()` callers remain unchanged.

Successful completion prints and displays
`RESULT: PASS (checked camera matrices)`, then leaves the result visible.
