# Direct GD-ROM disc-geometry validation

This example reads the complete 408-byte TOC and session-zero summary through
the drive packet interface, without using the BIOS command server. It
then uses KOS's existing `cdrom_locate_data_track()` on the direct TOC and
performs a BIOS TOC read as a control.

Success requires exact equality between the direct and BIOS
`cd_toc_t`, identical data-track FADs, a valid session count, agreement between
the session-zero lead-out and TOC lead-out, exact PIO transfer sizes, and clean
return from direct access to the BIOS-backed path.

Run it from a bootable CD-style data image with Flycast's serial console
enabled or on hardware with a debug transport. The example validates only the
low-density CD/CD-R path.

The 2026-08-16 Flycast interpreter run read tracks 1-2 and two sessions. The
complete direct and BIOS TOCs were byte-identical, both located the data track
at FAD 11,852, and the session and TOC lead-outs both reported FAD 328,059.
No SH-4 exception occurred.
