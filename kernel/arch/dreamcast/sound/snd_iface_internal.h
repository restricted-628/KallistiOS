/* KallistiOS ##version##

   snd_iface_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __SND_IFACE_INTERNAL_H
#define __SND_IFACE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

bool snd_iface_is_initialized(void);
int snd_iface_exclusive_begin(uint32_t timeout_ms);
void snd_iface_exclusive_end(void);

void snd_dsp_system_reset(void);
void snd_dsp_system_shutdown(void);

#endif /* __SND_IFACE_INTERNAL_H */
