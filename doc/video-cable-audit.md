# Dreamcast video and cable audit

## Scope

This audit covers display-cable detection, built-in mode selection, custom mode
validation, framebuffer sizing, and the boundary between safe policy and raw
timing configuration. PVR scene construction, polygon antialiasing, texture
filtering, and render synchronization are separate graphics concerns.

## Existing KOS baseline

KOS already detects the four connector states, exposes ten built-in timings,
supports generic resolution selection, permits caller-defined `vid_mode_t`
timings, programs the framebuffer and scan registers, manages multiple packed
framebuffers, controls blanking and border color, and exposes framebuffer
flipping and screenshots.

The main deficiency was not an inability to program the display. It was that
selection and installation had no status-bearing safety boundary:

- `vid_set_mode()` and `vid_set_mode_ex()` returned `void`;
- an invalid pixel mode indexed two fixed four-entry tables;
- invalid or zero framebuffer geometry could reach division and modulo paths;
- a direct cable-specific mode had its required cable overwritten before the
  compatibility check;
- VGA adjustment modified the caller's custom `vid_mode_t` in place;
- generic analog selection used the first table match, which preserved a 60 Hz
  default but offered no explicit 50 Hz regional policy;
- a custom mode using `CT_ANY` remained recorded as `CT_ANY`, so later PVR code
  could not reliably tell that the active connector was VGA;
- invalid initialization still cleared all VRAM after mode installation failed.

## Checked mode pipeline

The new pipeline separates planning from register mutation:

1. `vid_mode_resolve()` selects and normalizes a built-in mode for a supplied
   cable and refresh-standard preference without changing hardware.
2. `vid_mode_validate()` checks a complete mode against cable constraints,
   pixel-format bounds, register geometry, interlace requirements, framebuffer
   size, framebuffer count, and available VRAM.
3. `vid_set_mode_ex_checked()` copies the caller's mode, derives a tightly
   packed framebuffer size when needed, records the actual connector, performs
   VGA normalization on the private copy, and only then programs registers.

`vid_set_mode_checked()` preserves the established KOS generic table-order
policy. `vid_set_mode_standard_checked()` adds an explicit 50 or 60 Hz policy.
The legacy `void` setters remain as wrappers, so existing applications retain
their source contract while immediately gaining bounds checks.

Generic resolution prefers a cable-specific table entry before `CT_ANY`.
The explicit standard is ignored for VGA because the connector fixes the
60 Hz timing. Specific DM_* values remain explicit: the standard preference
only disambiguates generic requests.

Applications that want region-derived behavior can read their system region,
map Europe to `VID_MODE_STANDARD_50HZ` and other recognized retail regions to
`VID_MODE_STANDARD_60HZ`, then resolve or install the generic mode. KOS does not
silently change its startup policy, and an unknown region remains an
application decision rather than an undocumented guess.

`vid_get_mode()` copies the active mode into caller storage. This avoids making
new code depend on the mutable public `vid_mode` pointer while preserving that
legacy symbol for compatibility.

## Resource behavior and concurrency

Mode planning performs no allocation, starts no worker, and does not poll.
Cable detection is a bounded GPIO read. The hardware provides no cable-change
interrupt through this driver, so applications that permit live cable changes
must poll at a cadence appropriate to their UI.

Display-mode changes remain an application-controlled operation expected to
run outside interrupt context and outside active PVR rendering. This tranche
does not add a global video mutex or pretend that asynchronous mode replacement
is safe while other code owns framebuffer or PVR state.

## Remaining timing catalog

The newer capability surface distinguishes more combinations than KOS's
built-in table, including additional 320/640 width and 240/480 height forms,
interlaced and pseudo-non-interlaced analog output, extended PAL display
heights, and PAL60 variants.

Those names alone are not sufficient to create trustworthy `vid_mode_t`
entries. Each addition needs complete scanline, clock, bitmap, interrupt, and
border register values plus validation on real displays and capture hardware.
The checked resolver is designed to accept new built-ins as they are validated,
but this tranche deliberately does not invent missing timings or re-label an
existing timing as a different standard.

## Validation

`utils/video-mode-test` compiles the production resolver and validator against
host fixtures with strict C11 warnings enabled. It checks VGA precedence,
default and explicit 50/60-Hz analog selection, multibuffer sizing, specific
cable mismatch, unsupported standard requests, malformed pixel modes, odd
interlaced geometry, unknown flags, zero framebuffer counts, undersized
framebuffers, and null arguments.

The read-only `examples/dreamcast/video/mode-policy` program reports the
connected cable and current mode, derives a proposed standard from the system
region, and resolves the candidate mode without installing it. No video timing
is changed by the example.
