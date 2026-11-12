/* Minimum source-interface fixture; actual SH-4 build uses the real header. */
#ifndef TEST_PLAYER_SOUND_H
#define TEST_PLAYER_SOUND_H
#include <stddef.h>
#include <stdint.h>
#define SND_STREAM_BUFFER_MAX_PCM16 131008
#define SND_STREAM_INVALID (-1)
#define SND_CHANNEL_UPDATE_FREQUENCY 1u
#define SND_CHANNEL_UPDATE_VOLUME 2u
#define SND_CHANNEL_UPDATE_PAN 4u
typedef int snd_stream_hnd_t;
typedef void *(*snd_stream_callback_t)(snd_stream_hnd_t,int,int *);
typedef struct snd_stream_config {
    uint32_t sample_rate, transfer_timeout_ms;
    uint8_t channels;
    struct {uint8_t volume,pan;} channel[2];
} snd_stream_config_t;
typedef struct snd_stream_status {int last_error;} snd_stream_status_t;
int snd_stream_config_init(snd_stream_config_t *);
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t,int);
void snd_stream_set_userdata(snd_stream_hnd_t,void *);
void *snd_stream_get_userdata(snd_stream_hnd_t);
void snd_stream_set_callback(snd_stream_hnd_t,snd_stream_callback_t);
int snd_stream_start_ex(snd_stream_hnd_t,const snd_stream_config_t *);
int snd_stream_poll_ex(snd_stream_hnd_t);
int snd_stream_update(snd_stream_hnd_t,const snd_stream_config_t *,uint32_t);
int snd_stream_stop_ex(snd_stream_hnd_t,uint32_t);
int snd_stream_get_status(snd_stream_hnd_t,snd_stream_status_t *);
#endif
