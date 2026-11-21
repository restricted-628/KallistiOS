#ifndef KOS_AUDIO_ADX_LIVE_TL_INTERFACE_H
#define KOS_AUDIO_ADX_LIVE_TL_INTERFACE_H

#include "snd_retained_pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

enum snd_retained_gain_state {
    SND_RETAINED_GAIN_PENDING,
    SND_RETAINED_GAIN_APPLIED,
    SND_RETAINED_GAIN_FAILED
};

#define SND_RETAINED_GAIN_LEFT  1u
#define SND_RETAINED_GAIN_RIGHT 2u

typedef struct snd_retained_gain_status {
    uint64_t epoch, request;
    uint32_t session;
    enum snd_retained_gain_state state;
    unsigned admitted, applied, rejected, unknown;
    unsigned acknowledged; /* Channels with a prior acknowledged value. */
    uint8_t requested_tl[2], acknowledged_tl[2];
    int error;
} snd_retained_gain_status_t;

/* Local admission for a playing fixed-rate retained mono or stereo lease. TL is the
   raw 0..255 hardware attenuation byte (0 = unity setting, 255 = maximum
   attenuation setting). This is neither linear gain nor a hard-mute promise.
   Validate both arguments before admission. Mono uses left_tl and ignores
   the valid right_tl. Success issues a nonzero lease-local request and
   changes no hardware. Retry work by calling the existing
   snd_retained_pcm_poll; it services this request before normal refill.
   A pending retry can return poll success with error in the gain snapshot.
   Source-clock queries during rotation report the last qualified prefix as
   invalid until all active channels' fresh observations qualify. Terminal
   errors require stop. Stereo completion means both channels acknowledged,
   potentially at different samples. No newer request while pending (EBUSY).
   ENOTSUP without firmware capability. Failure leaves *request and prior
   status unchanged.
   Same serialized ordinary owner and non-reentrant contract as the PCM API. */
int snd_retained_pcm_gain_begin(snd_retained_pcm_t *pcm, unsigned left_tl,
    unsigned right_tl, uint64_t *request);

/* No I/O. Only the latest admitted request is retained. Zero is EINVAL and a
   different request is ENOENT. Masks are left bit 0 and right bit 1. For mono
   the right mask bits and both right TL bytes are zero. Applied and rejected
   classify ARM outcomes; unknown covers admitted commands
   without a qualified outcome. Acknowledged values are historical and may be
   obsolete on unknown channels. error is zero, a retryable pending reason,
   or the terminal failure. Stop cancels further gain admission and settles
   already admitted command bookkeeping. It may leave abandoned outcomes
   unknown. Negative returns leave *out unchanged. */
int snd_retained_pcm_gain_status(const snd_retained_pcm_t *pcm, uint64_t request,
    snd_retained_gain_status_t *out);

#ifdef __cplusplus
}
#endif
#endif
