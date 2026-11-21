#ifndef KOS_AUDIO_ADX_START_TL_INTERFACE_H
#define KOS_AUDIO_ADX_START_TL_INTERFACE_H

#include "snd_retained_pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opt in to initial hardware total level on a new retained PCM lease.
   Both arguments must be raw TL bytes in 0..255. Mono uses left_tl.
   0 is the unity register setting and 255 the maximum attenuation setting.
   Use the same serialized ordinary owner as snd_retained_pcm_start().

   EINVAL for bad arguments or a used lease, EPERM in interrupt context,
   ENOTSUP without checked raw TL firmware. These preflight failures on a new
   lease leave it unstarted and closable. Once a start is attempted, use the
   ordinary retained stop contract after any failure. Neither channel is
   keyed on until all delayed configurations and TL writes are acknowledged
   while key-off. A failure after synchronized start can leave the pair
   running until checked stop. Success qualifies the first source cursor
   with the requested TL and does not create a live gain request. */
int snd_retained_pcm_start_with_tl(snd_retained_pcm_t *pcm, unsigned left_tl,
    unsigned right_tl);

#ifdef __cplusplus
}
#endif
#endif
