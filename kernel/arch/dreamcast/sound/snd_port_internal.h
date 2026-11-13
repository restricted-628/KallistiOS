/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#ifndef __SND_PORT_INTERNAL_H
#define __SND_PORT_INTERNAL_H
#include <stdbool.h>
#include <dc/sound/port.h>

typedef struct snd_port_transaction {
    snd_port_t port;
    uint32_t serial;
} snd_port_transaction_t;

/* Private software policy, not a playback API. Priority is 0..15.
   Inactive, either-zero and equal/higher admit; lower nonzero declines.
   Begin returns 1 admitted, 0 declined, -1 error (EINVAL, EBUSY, EOVERFLOW).
   Output/state are unchanged on decline/error. One pending transaction pins
   the port. A serial never wraps during a handle's lifetime.
   Finish commits priority only if succeeded; false abandons without changing
   committed state. Returns 0, or -1/EINVAL for stale/duplicate/mismatched token.
   Playback keeps an admitted START pending until ARM classification and finishes
   exactly once: false on known refusal/rejection, true on APPLIED. Timeout is
   neither. Committed policy state is not hardware/sample retirement. */
int snd_port_begin(snd_port_t port, unsigned priority,
                   snd_port_transaction_t *transaction);
int snd_port_finish(snd_port_transaction_t transaction, bool succeeded);
/* Playback attachment pins are separate from one outstanding START transaction.
   All hooks revalidate the opaque generation under IRQ exclusion. */
#define SND_PORT_SLOT_COUNT 64u
#define SND_PORT_SLOT_INDEX(port) ((port) & (SND_PORT_SLOT_COUNT - 1u))
int snd_port_playback_pin(snd_port_t port, unsigned *channel);
int snd_port_playback_validate(snd_port_t port);
int snd_port_playback_unpin(snd_port_t port);
int snd_port_playback_stopped(snd_port_t port);
#endif
