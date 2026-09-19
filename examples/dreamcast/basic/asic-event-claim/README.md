# ASIC event claim probe

This target-side smoke test claims the otherwise unused GD-ROM illegal-address
error event, inspects its status, masks and unmasks it through the claim token,
and releases it. It does not provoke a hardware error or perform disc I/O.

The program prints `ASIC-EVENT-CLAIM: PASS` when the complete ownership
lifecycle succeeds. `BUSY` means another driver already owns or enables that
event; the probe leaves that state untouched.
