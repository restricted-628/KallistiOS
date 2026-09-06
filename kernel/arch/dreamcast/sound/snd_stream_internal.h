/* KallistiOS ##version##

   snd_stream_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __SND_STREAM_INTERNAL_H
#define __SND_STREAM_INTERNAL_H

#include <stdbool.h>

#include <dc/sound/stream.h>

int _snd_stream_service_claim(snd_stream_hnd_t hnd, const void *owner);
int _snd_stream_service_release(snd_stream_hnd_t hnd, const void *owner);
int _snd_stream_service_poll(snd_stream_hnd_t hnd, const void *owner,
                             bool *underrun);

#endif /* __SND_STREAM_INTERNAL_H */
