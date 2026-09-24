# Backend default regression probe

Checks `/cd`'s initial direct selection, explicit BIOS opt-in, invalid selection
rejection, and reset to direct on filesystem reinitialization. It also checks
default/direct and explicit/BIOS raw-range identity and staged-session routing.

Mount-selection spies report ordinary CD, XA CD, and GD-ROM media. In all six
backend/media cases, the real ISO9660 open path must issue exactly one
low-density TOC request. The spy then fails that request before sector I/O;
this verifies policy and absence of a high-density retry, not successful mounts.

The two session-submission endpoints are replaced with link-time spies: this
probe does not queue staged reads or validate DMA, media recognition, physical
drive behavior, or recovery. Normal KOS boot initialization still runs.

Build with `make` in a configured KOS environment. Success prints
`DISC-DEFAULTS: PASS checks=32`.
