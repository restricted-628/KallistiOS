#ifndef MEDIA_ADX_LOADER_INTERNAL_H
#define MEDIA_ADX_LOADER_INTERNAL_H

#include <kosdec/adx_loader.h>

#define MEDIA_ADX_RELOAD_DEFAULT UINT32_MAX

uint32_t media_adx_default_reload_sectors(size_t capacity);
float media_adx_input_seconds(uint32_t bytes, unsigned channels, unsigned rate);
bool media_adx_reload_time_sectors(float seconds, unsigned channels,
    unsigned rate, uint32_t capacity_sectors, uint32_t *out);
enum kosdec_adx_loader_status media_adx_loader_start_linked(
    struct kosdec_adx_loader *, uint32_t sectors, bool clamp);

#endif
