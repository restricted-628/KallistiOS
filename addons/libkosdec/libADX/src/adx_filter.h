/* Original post-decode hook; called only with owned, complete PCM output. */
#ifndef MEDIA_ADX_FILTER_H
#define MEDIA_ADX_FILTER_H
#include <kosdec/adx.h>
static inline void media_adx_filter_pcm(kosdec_adx_pcm_filter filter, void *context,
    unsigned channels, int16_t *output, size_t frames) {
    if(filter && frames)
        for(unsigned ch = 0; ch < channels; ++ch)
            filter(context, ch, output + ch, frames, channels);
}
#endif
