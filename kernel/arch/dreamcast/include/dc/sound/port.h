/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
/** \file dc/sound/port.h
    \brief Cooperative software ownership of sound channels.

    Ports share accounting with snd_sfx_chn_alloc/free and streams. Raw channel
    clients must coordinate access and never free another owner's channels.
    Reservation does not establish silence: previously selected or playing
    automatic SFX may overlap. Coordinate legacy playback externally.

    These functions do not submit commands or own samples. Closing returns
    software reservations only; it proves neither playback completion nor sample
    reuse safety. Existing raw playback/sample lifetime obligations still apply.
*/
#ifndef __DC_SOUND_PORT_H
#define __DC_SOUND_PORT_H
#include <stdint.h>
#include <sys/cdefs.h>
__BEGIN_DECLS

typedef uint32_t snd_port_t;
#define SND_PORT_INVALID ((snd_port_t)0)

typedef struct snd_port_info {
    unsigned channel_count;
    int channels[2]; /**< Unused second channel is -1. Pair need not be adjacent. */
} snd_port_info_t;

/** Reserve one or two channels atomically, without stealing.
    Returns 0 on success, -1 with errno: EINVAL for invalid arguments, ENOSPC
    for capacity, EOVERFLOW when exhausted handle generations prevent reuse.
    Output is unchanged on failure. No initialization/shutdown call is needed.
    Handles are opaque, scoped to this runtime and never wrap into old handles;
    an exhausted slot is retired until runtime restart. */
int snd_port_reserve(unsigned channel_count, snd_port_t *port);

/** Query a live reservation. Returns 0, or -1/EINVAL for invalid/stale handle
    or NULL output. Output is unchanged on failure. */
int snd_port_query(snd_port_t port, snd_port_info_t *info);

/** Return all channels owned by a port. Returns 0, or -1 with errno EINVAL
    for an invalid/stale handle, EBUSY for an outstanding internal transaction or playback attachment.
    Failure leaves ownership intact. Does not stop playback or free samples. */
int snd_port_close(snd_port_t port);
__END_DECLS
#endif
