#ifndef MEDIA_ADX_CONTAINER_INTERNAL_H
#define MEDIA_ADX_CONTAINER_INTERNAL_H

#include <kosdec/adx_pipe.h>

/* Staged readers parse/decode from their own bounded windows. Only the FILE
   step calls the provider. The ordinary public reader combines both steps. */
struct kosdec_adx_wave_result media_adx_wave_read(struct kosdec_adx_wave *,
    int16_t *, size_t, bool staged);
struct kosdec_adx_aiff_result media_adx_aiff_read(struct kosdec_adx_aiff *,
    int16_t *, size_t, bool staged);
struct kosdec_adx_au_result media_adx_au_read(struct kosdec_adx_au *,
    int16_t *, size_t, bool staged);
struct kosdec_adx_spsd_result media_adx_spsd_read(struct kosdec_adx_spsd *,
    int16_t *, size_t, bool staged);

enum kosdec_adx_status media_adx_wave_step_file(struct kosdec_adx_wave *);
enum kosdec_adx_status media_adx_aiff_step_file(struct kosdec_adx_aiff *);
enum kosdec_adx_status media_adx_au_step_file(struct kosdec_adx_au *);
enum kosdec_adx_status media_adx_spsd_step_file(struct kosdec_adx_spsd *);

/* Keep a provider's maximum individual request independent of the window. */
#define MEDIA_ADX_CONTAINER_CHUNK 512u
enum kosdec_adx_status media_adx_container_fill(kosdec_adx_read_at, void *context,
    uint8_t *buffer, unsigned *used, uint32_t *offset, unsigned bytes);
void media_adx_container_compact(uint8_t *buffer, unsigned *used, unsigned *position);
enum kosdec_adx_status media_adx_container_stage(kosdec_adx_read_at, void *context,
    uint8_t *buffer, unsigned capacity, unsigned *used, uint32_t *offset, uint32_t end);

static inline bool media_adx_pipe_has_container(const struct kosdec_adx_pipe *pipe) {
    return pipe->wave || pipe->aiff || pipe->au || pipe->spsd;
}

static inline unsigned media_adx_container_pcm_request(unsigned room, uint32_t remaining) {
    return room < remaining ? room : remaining;
}

#endif
