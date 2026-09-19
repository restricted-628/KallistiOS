# Fiber service synchronization probe

This regression proves that the general cooperative event and mutex primitives
integrate with a fiber service executor. One service yields while owning a
cooperative mutex; a second service parks on that mutex, receives FIFO ownership,
and sets an event which resumes the first service.

The executor automatically observes generic fiber wait and ready transitions;
service code does not need a second service-specific synchronization API.

Success prints `KOSFIBERSERVICESYNC sequence=6`.
