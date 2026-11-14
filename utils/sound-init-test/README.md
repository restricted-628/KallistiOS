# Firmware startup readiness

`make test sanitize` compiles the exact production startup, queue geometry and
remaining-time bodies. Only OS scheduling/time, MMIO reads and the status-query
transport are faked. The fixture exercises independent delayed queue publication,
never-ready and malformed headers, budget exhaustion before query admission,
one query with the remaining budget, post-admission ENODEV without retry, and
existing shutdown/errno failure cleanup. It is not an ARM boot timing test.
