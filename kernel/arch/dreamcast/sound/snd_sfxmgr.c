/* KallistiOS ##version##

   snd_sfxmgr.c
   Copyright (C) 2000, 2001, 2002, 2003, 2004 Megan Potter
   Copyright (C) 2023, 2024 Ruslan Rostovtsev
   Copyright (C) 2023 Andy Barajas
   Copyright (C) 2024 Stefanos Kornilios Mitsis Poiitidis
   Copyright (C) 2026 Joseph Black

   Sound effects management system; this thing loads and plays sound effects
   during game operation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <sys/queue.h>
#include <sys/ioctl.h>
#include <kos/dbglog.h>
#include <kos/fs.h>
#include <kos/irq.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>

#include "arm/aica_cmd_iface.h"

struct snd_effect;
LIST_HEAD(selist, snd_effect);

typedef struct snd_effect {
    uint32_t  locl, locr;
    uint32_t  len;
    uint32_t  rate;
    uint32_t  used;
    uint32_t  fmt;
    uint16_t  stereo;

    LIST_ENTRY(snd_effect)  list;
} snd_effect_t;

struct selist snd_effects;

/* The next channel we'll use to play sound effects. */
static int sfx_nextchan = 0;

/* Our channel-in-use mask. */
static uint64_t sfx_inuse = 0;

static snd_effect_t *find_snd_effect(sfxhnd_t handle) {
    snd_effect_t *effect;

    LIST_FOREACH(effect, &snd_effects, list) {
        if((sfxhnd_t)effect == handle)
            return effect;
    }

    return NULL;
}

/* Unload all loaded samples and free their SPU RAM */
void snd_sfx_unload_all(void) {
    snd_effect_t *t;

    while((t = LIST_FIRST(&snd_effects)))
        snd_sfx_unload((sfxhnd_t)t);
}

/* Unload a single sample */
void snd_sfx_unload(sfxhnd_t idx) {
    snd_effect_t *t = find_snd_effect(idx);

    if(!t) {
        errno = EINVAL;
        return;
    }

    snd_mem_free(t->locl);

    if(t->stereo)
        snd_mem_free(t->locr);

    LIST_REMOVE(t, list);
    free(t);
}

typedef struct {
    uint8_t riff[4];
    uint32_t totalsize;
    uint8_t riff_format[4];
} wavmagic_t;

typedef struct {
    uint8_t id[4];
    uint32_t size;
} chunkhdr_t;

typedef struct {
    int16_t format;
    int16_t channels;
    int32_t sample_rate;
    int32_t byte_per_sec;
    int16_t blocksize;
    int16_t sample_size;
} fmthdr_t;

/* WAV header */
typedef struct {
    wavmagic_t magic;

    chunkhdr_t chunk;

    fmthdr_t fmt;
} wavhdr_t;

/* WAV sample formats */
#define WAVE_FMT_PCM                   0x0001 /* PCM */
#define WAVE_FMT_YAMAHA_ADPCM_ITU_G723 0x0014 /* ITU G.723 Yamaha ADPCM (KallistiOS) */
#define WAVE_FMT_YAMAHA_ADPCM          0x0020 /* Yamaha ADPCM (ffmpeg) */

_Static_assert(sizeof(wavmagic_t) == 12, "Unexpected RIFF header layout");
_Static_assert(sizeof(chunkhdr_t) == 8, "Unexpected RIFF chunk layout");
_Static_assert(sizeof(fmthdr_t) == 16, "Unexpected WAVE format layout");

static int wav_fail(void) {
    errno = EILSEQ;
    return -1;
}

static int read_exact(file_t fd, void *buffer, size_t size) {
    uint8_t *output = buffer;

    while(size) {
        ssize_t count = fs_read(fd, output, size);

        if(count <= 0)
            return -1;

        output += count;
        size -= (size_t)count;
    }

    return 0;
}

static int read_wav_header(file_t fd, size_t file_size, wavhdr_t *wavhdr) {
    size_t offset = sizeof(wavmagic_t);
    size_t riff_end;
    bool have_format = false;

    memset(wavhdr, 0, sizeof(*wavhdr));

    if(file_size < sizeof(wavmagic_t) ||
       read_exact(fd, &wavhdr->magic, sizeof(wavhdr->magic)) < 0)
        return wav_fail();

    if(memcmp(wavhdr->magic.riff, "RIFF", 4) ||
       memcmp(wavhdr->magic.riff_format, "WAVE", 4) ||
       wavhdr->magic.totalsize < 4 ||
       wavhdr->magic.totalsize > SIZE_MAX - 8)
        return wav_fail();

    riff_end = (size_t)wavhdr->magic.totalsize + 8;

    if(riff_end > file_size)
        return wav_fail();

    while(offset <= riff_end && riff_end - offset >= sizeof(chunkhdr_t)) {
        size_t padded_size;

        /* Read the chunk header */
        if(read_exact(fd, &wavhdr->chunk, sizeof(wavhdr->chunk)) < 0)
            return wav_fail();

        offset += sizeof(chunkhdr_t);
        padded_size = (size_t)wavhdr->chunk.size + (wavhdr->chunk.size & 1u);

        if(padded_size < wavhdr->chunk.size || padded_size > riff_end - offset)
            return wav_fail();

        /* If it is the fmt chunk, grab the fields we care about and skip the
           rest of the section if there is more */
        if(memcmp(wavhdr->chunk.id, "fmt ", 4) == 0) {
            if(wavhdr->chunk.size < sizeof(wavhdr->fmt) ||
               read_exact(fd, &wavhdr->fmt, sizeof(wavhdr->fmt)) < 0)
                return wav_fail();

            if(fs_seek(fd, padded_size - sizeof(wavhdr->fmt), SEEK_CUR) < 0)
                return wav_fail();

            have_format = true;
        }
        /* If we found the data chunk, we are done */
        else if(memcmp(wavhdr->chunk.id, "data", 4) == 0) {
            return have_format ? 0 : wav_fail();
        }
        /* Skip meta data */
        else {
            if(fs_seek(fd, padded_size, SEEK_CUR) < 0)
                return wav_fail();
        }

        offset += padded_size;
    }

    return wav_fail();
}

static int read_wav_header_buf(const void *buffer, size_t buffer_size,
                               wavhdr_t *wavhdr, size_t *data_offset) {
    const uint8_t *bytes = buffer;
    size_t offset = sizeof(wavmagic_t);
    size_t riff_end;
    bool have_format = false;

    if(!buffer || buffer_size < sizeof(wavmagic_t)) {
        errno = EINVAL;
        return -1;
    }

    memset(wavhdr, 0, sizeof(*wavhdr));
    memcpy(&wavhdr->magic, bytes, sizeof(wavhdr->magic));

    if(memcmp(wavhdr->magic.riff, "RIFF", 4) ||
       memcmp(wavhdr->magic.riff_format, "WAVE", 4) ||
       wavhdr->magic.totalsize < 4 ||
       wavhdr->magic.totalsize > SIZE_MAX - 8)
        return wav_fail();

    riff_end = (size_t)wavhdr->magic.totalsize + 8;

    if(riff_end > buffer_size)
        return wav_fail();

    while(offset <= riff_end && riff_end - offset >= sizeof(chunkhdr_t)) {
        size_t padded_size;

        memcpy(&wavhdr->chunk, bytes + offset, sizeof(wavhdr->chunk));
        offset += sizeof(chunkhdr_t);
        padded_size = (size_t)wavhdr->chunk.size + (wavhdr->chunk.size & 1u);

        if(padded_size < wavhdr->chunk.size || padded_size > riff_end - offset)
            return wav_fail();

        /* If it is the fmt chunk, grab the fields we care about and skip the
           rest of the section if there is more */
        if(memcmp(wavhdr->chunk.id, "fmt ", 4) == 0) {
            if(wavhdr->chunk.size < sizeof(wavhdr->fmt))
                return wav_fail();

            memcpy(&wavhdr->fmt, bytes + offset, sizeof(wavhdr->fmt));
            have_format = true;
        }
        /* If we found the data chunk, we are done */
        else if(memcmp(wavhdr->chunk.id, "data", 4) == 0) {
            if(!have_format)
                return wav_fail();

            *data_offset = offset;
            return 0;
        }

        offset += padded_size;
    }

    return wav_fail();
}

static uint8_t *read_wav_data(file_t fd, wavhdr_t *wavhdr) {
    size_t allocation_size;

    if(!wavhdr->chunk.size || wavhdr->chunk.size > SIZE_MAX - 31)
        return NULL;

    allocation_size = __align_up((size_t)wavhdr->chunk.size, 32);

    /* Allocate memory for WAV data */
    uint8_t *wav_data = aligned_alloc(32, allocation_size);

    if(wav_data == NULL)
        return NULL;

    memset(wav_data + wavhdr->chunk.size, 0,
           allocation_size - wavhdr->chunk.size);

    /* Read WAV data */
    if(read_exact(fd, wav_data, wavhdr->chunk.size) < 0) {
        dbglog(DBG_WARNING, "snd_sfx: file has not been fully read.\n");
        free(wav_data);
        errno = EILSEQ;
        return NULL;
    }

    return wav_data;
}

static int validate_wav_format(const wavhdr_t *wavhdr, uint32_t *sample_count,
                               size_t *channel_bytes, uint32_t *aica_format) {
    const uint32_t data_size = wavhdr->chunk.size;
    const uint16_t channels = wavhdr->fmt.channels;
    uint64_t samples;
    size_t bytes;

    if((channels != 1 && channels != 2) || wavhdr->fmt.sample_rate <= 0 ||
       (uint32_t)wavhdr->fmt.sample_rate > (UINT32_MAX >> 10) || !data_size)
        return wav_fail();

    if(wavhdr->fmt.format == WAVE_FMT_PCM) {
        size_t frame_bytes;

        if(wavhdr->fmt.sample_size != 8 && wavhdr->fmt.sample_size != 16)
            return wav_fail();

        frame_bytes = (size_t)channels * (wavhdr->fmt.sample_size / 8u);

        if(data_size % frame_bytes)
            return wav_fail();

        samples = data_size / frame_bytes;
        bytes = data_size / channels;
        *aica_format = wavhdr->fmt.sample_size == 8 ? AICA_SM_8BIT :
                                                     AICA_SM_16BIT;
    }
    else if(wavhdr->fmt.format == WAVE_FMT_YAMAHA_ADPCM_ITU_G723 ||
            wavhdr->fmt.format == WAVE_FMT_YAMAHA_ADPCM) {
        if(wavhdr->fmt.sample_size != 4 ||
           (channels == 2 &&
            wavhdr->fmt.format == WAVE_FMT_YAMAHA_ADPCM_ITU_G723 &&
            (data_size & 1)))
            return wav_fail();

        samples = ((uint64_t)data_size * 2u) / channels;
        bytes = channels == 1 ? data_size : (data_size + 1u) / 2u;
        *aica_format = AICA_SM_ADPCM;
    }
    else {
        return wav_fail();
    }

    if(!samples || samples > 65534)
        return wav_fail();

    *sample_count = (uint32_t)samples;
    *channel_bytes = bytes;
    return 0;
}

static uint8_t *alloc_sample_buffer(size_t size) {
    uint8_t *buffer;
    size_t allocation_size;

    if(!size || size > SIZE_MAX - 31) {
        errno = EOVERFLOW;
        return NULL;
    }

    allocation_size = __align_up(size, 32);
    buffer = aligned_alloc(32, allocation_size);

    if(buffer)
        memset(buffer, 0, allocation_size);

    return buffer;
}

static snd_effect_t *create_snd_effect(const wavhdr_t *wavhdr,
                                       const uint8_t *wav_data) {
    snd_effect_t *effect = NULL;
    uint8_t *left = NULL;
    uint8_t *right = NULL;
    uint32_t sample_count;
    uint32_t aica_format;
    size_t channel_bytes;
    size_t i;
    int saved_errno;

    if(validate_wav_format(wavhdr, &sample_count, &channel_bytes,
                           &aica_format) < 0)
        return NULL;

    effect = calloc(1, sizeof(*effect));

    if(!effect)
        return NULL;

    left = alloc_sample_buffer(channel_bytes);

    if(!left)
        goto fail;

    if(wavhdr->fmt.channels == 2) {
        right = alloc_sample_buffer(channel_bytes);

        if(!right)
            goto fail;
    }

    if(wavhdr->fmt.channels == 1) {
        memcpy(left, wav_data, channel_bytes);
    }
    else if(wavhdr->fmt.format == WAVE_FMT_PCM &&
            wavhdr->fmt.sample_size == 16) {
        for(i = 0; i < sample_count; ++i) {
            memcpy(left + i * 2, wav_data + i * 4, 2);
            memcpy(right + i * 2, wav_data + i * 4 + 2, 2);
        }
    }
    else if(wavhdr->fmt.format == WAVE_FMT_PCM) {
        for(i = 0; i < sample_count; ++i) {
            left[i] = wav_data[i * 2];
            right[i] = wav_data[i * 2 + 1];
        }
    }
    else if(wavhdr->fmt.format == WAVE_FMT_YAMAHA_ADPCM_ITU_G723) {
        memcpy(left, wav_data, channel_bytes);
        memcpy(right, wav_data + channel_bytes, channel_bytes);
    }
    else {
        /* Interleaved stereo ADPCM stores right then left nibbles in each
           byte. Repack each channel without requiring a 32-byte multiple. */
        if(!right) {
            errno = EPROTO;
            goto fail;
        }

        for(i = 0; i < sample_count; ++i) {
            const uint8_t packed = wav_data[i];
            const unsigned shift = (i & 1u) ? 4 : 0;

            right[i / 2] |= (packed & 0x0f) << shift;
            left[i / 2] |= (packed >> 4) << shift;
        }
    }

    effect->locl = snd_mem_malloc(channel_bytes);

    if(!effect->locl)
        goto fail;

    if(wavhdr->fmt.channels == 2) {
        effect->locr = snd_mem_malloc(channel_bytes);

        if(!effect->locr)
            goto fail;
    }

    effect->rate = (uint32_t)wavhdr->fmt.sample_rate;
    effect->stereo = wavhdr->fmt.channels == 2;
    effect->fmt = aica_format;
    effect->len = sample_count;

    spu_memload_sq(effect->locl, left, channel_bytes);

    if(effect->stereo)
        spu_memload_sq(effect->locr, right, channel_bytes);

    free(left);
    free(right);
    return effect;

fail:
    saved_errno = errno;

    if(effect) {
        if(effect->locl)
            snd_mem_free(effect->locl);

        if(effect->locr)
            snd_mem_free(effect->locr);
    }

    free(left);
    free(right);
    free(effect);
    errno = saved_errno;
    return NULL;
}

/* Load a sound effect from a WAV file and return a handle to it */
sfxhnd_t snd_sfx_load(const char *fn) {
    file_t fd;
    wavhdr_t wavhdr;
    snd_effect_t *effect;
    uint8_t *wav_data;
    off_t file_size;

    if(!fn) {
        errno = EINVAL;
        return SFXHND_INVALID;
    }

    /* Open the sound effect file */
    fd = fs_open(fn, O_RDONLY);
    if(fd == FILEHND_INVALID) {
        dbglog(DBG_ERROR, "snd_sfx_load: can't open %s\n", fn);
        return SFXHND_INVALID;
    }

    file_size = fs_total(fd);

    /* Read WAV header */
    if(file_size < 0 || read_wav_header(fd, (size_t)file_size, &wavhdr) < 0) {
        fs_close(fd);
        dbglog(DBG_ERROR, "snd_sfx_load: can't read wav header %s\n", fn);
        return SFXHND_INVALID;
    }

    /* Read WAV data */
    wav_data = read_wav_data(fd, &wavhdr);
    fs_close(fd);
    if(!wav_data)
        return SFXHND_INVALID;

    /* Create and initialize sound effect */
    effect = create_snd_effect(&wavhdr, wav_data);
    if(!effect) {
        free(wav_data);
        return SFXHND_INVALID;
    }

    /* Finish up and return the sound effect handle */
    free(wav_data);
    LIST_INSERT_HEAD(&snd_effects, effect, list);

    return (sfxhnd_t)effect;
}

sfxhnd_t snd_sfx_load_ex(const char *fn, uint32_t rate, uint16_t bitsize, uint16_t channels) {
    sfxhnd_t effect;
    file_t fd;
    off_t total;

    if(!fn) {
        errno = EINVAL;
        return SFXHND_INVALID;
    }

    fd = fs_open(fn, O_RDONLY);

    if(fd == FILEHND_INVALID) {
        dbglog(DBG_ERROR, "snd_sfx_load_ex: can't open sfx %s\n", fn);
        return SFXHND_INVALID;
    }
    total = fs_total(fd);

    if(total < 0)
        effect = SFXHND_INVALID;
    else
        effect = snd_sfx_load_fd(fd, (size_t)total, rate, bitsize, channels);

    fs_close(fd);
    return effect;
}

static int raw_sample_geometry(size_t len, uint32_t rate, uint16_t bitsize,
                               uint16_t channels, size_t *channel_bytes,
                               uint32_t *sample_count, uint32_t *format) {
    size_t bytes;
    uint64_t samples;

    if((channels != 1 && channels != 2) || !rate ||
       rate > (UINT32_MAX >> 10) || !len || len % channels)
        return wav_fail();

    bytes = len / channels;

    switch(bitsize) {
        case 4:
            samples = (uint64_t)bytes * 2u;
            *format = AICA_SM_ADPCM;
            break;
        case 8:
            samples = bytes;
            *format = AICA_SM_8BIT;
            break;
        case 16:
            if(bytes & 1)
                return wav_fail();

            samples = bytes / 2u;
            *format = AICA_SM_16BIT;
            break;
        default:
            return wav_fail();
    }

    if(!samples || samples > 65534)
        return wav_fail();

    *channel_bytes = bytes;
    *sample_count = (uint32_t)samples;
    return 0;
}

sfxhnd_t snd_sfx_load_fd(file_t fd, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels) {
    snd_effect_t *effect;
    size_t chan_len, allocation_size;
    uint32_t sample_count, format;
    // uint32_t fs_rootbus_dma_ready = 0;
    // uint32_t fs_dma_len = 0;
    uint8_t *tmp_buff = NULL;

    if(fd == FILEHND_INVALID ||
       raw_sample_geometry(len, rate, bitsize, channels, &chan_len,
                           &sample_count, &format) < 0)
        return SFXHND_INVALID;

    effect = malloc(sizeof(snd_effect_t));

    if(effect == NULL) {
        return SFXHND_INVALID;
    }

    memset(effect, 0, sizeof(snd_effect_t));

    effect->rate = rate;
    effect->stereo = channels == 2;
    effect->fmt = format;
    effect->len = sample_count;

    effect->locl = snd_mem_malloc(chan_len);

    if(!effect->locl) {
        goto err_occurred;
    }
    allocation_size = __align_up(chan_len, 32);
    /* Uncomment when implementation is merged.
    if(fs_ioctl(fd, IOCTL_FS_ROOTBUS_DMA_READY, &fs_dma_len) == 0) {
        if(chan_len >= fs_dma_len) {
            fs_rootbus_dma_ready = 1;
        }
    }
    if(fs_rootbus_dma_ready) {
        read_len = chan_len & ~(fs_dma_len - 1);

        if(fs_read(fd, (void *)(effect->locl | SPU_RAM_UNCACHED_BASE), read_len) <= 0) {
            goto err_occurred;
        }
        read_len = chan_len - read_len;
    }
    */
    if(chan_len > 0) {
        tmp_buff = aligned_alloc(32, allocation_size);

        if(!tmp_buff)
            goto err_occurred;

        memset(tmp_buff, 0, allocation_size);

        if(read_exact(fd, tmp_buff, chan_len) < 0) {
            errno = EILSEQ;
            goto err_occurred;
        }
        spu_memload_sq(effect->locl, tmp_buff, chan_len);
    }

    if(channels > 1) {
        effect->locr = snd_mem_malloc(chan_len);

        if(!effect->locr) {
            goto err_occurred;
        }
        /* Uncomment when implementation is merged.
        if(fs_rootbus_dma_ready) {
            read_len = chan_len & ~(fs_dma_len - 1);

            if(fs_read(fd, (void *)(effect->locr | SPU_RAM_UNCACHED_BASE), chan_len) <= 0) {
                goto err_occurred;
            }
            read_len = chan_len - read_len;
        }
        */
        if(chan_len > 0) {
            memset(tmp_buff, 0, allocation_size);

            if(read_exact(fd, tmp_buff, chan_len) < 0) {
                errno = EILSEQ;
                goto err_occurred;
            }
            spu_memload_sq(effect->locr, tmp_buff, chan_len);
        }
    }

    if(tmp_buff) {
        free(tmp_buff);
    }
    LIST_INSERT_HEAD(&snd_effects, effect, list);
    return (sfxhnd_t)effect;

err_occurred:
    if(effect->locl)
        snd_mem_free(effect->locl);
    if(effect->locr)
        snd_mem_free(effect->locr);
    if(tmp_buff)
        free(tmp_buff);

    free(effect);
    return SFXHND_INVALID;
}

/* Load a sound effect from a bounded WAV buffer and return a handle to it. */
sfxhnd_t snd_sfx_load_wav_buf(const void *buffer, size_t buffer_size) {
    wavhdr_t wavhdr;
    snd_effect_t *effect;
    size_t data_offset;

    if(!buffer) {
        errno = EINVAL;
        return SFXHND_INVALID;
    }

    /* Read WAV header */
    if(read_wav_header_buf(buffer, buffer_size, &wavhdr, &data_offset) < 0)
        return SFXHND_INVALID;

    /* Create and initialize sound effect */
    effect = create_snd_effect(&wavhdr,
                               (const uint8_t *)buffer + data_offset);

    if(!effect)
        return SFXHND_INVALID;

    LIST_INSERT_HEAD(&snd_effects, effect, list);
    return (sfxhnd_t)effect;
}

/* Legacy unbounded entry point. The RIFF-declared length is the only bound
   available here; new code should use snd_sfx_load_wav_buf(). */
sfxhnd_t snd_sfx_load_buf(char *buf) {
    wavmagic_t magic;
    size_t declared_size;

    if(!buf) {
        errno = EINVAL;
        return SFXHND_INVALID;
    }

    memcpy(&magic, buf, sizeof(magic));

    if(magic.totalsize > SIZE_MAX - 8)
        return wav_fail();

    declared_size = (size_t)magic.totalsize + 8;
    return snd_sfx_load_wav_buf(buf, declared_size);
}

sfxhnd_t snd_sfx_load_raw_buf(char *buf, size_t len, uint32_t rate, uint16_t bitsize, uint16_t channels) {
    snd_effect_t *effect;
    size_t chan_len, allocation_size;
    uint32_t sample_count, format;
    uint8_t *tmp_buff = NULL;
    size_t bufidx = 0;

    if(!buf || raw_sample_geometry(len, rate, bitsize, channels, &chan_len,
                                   &sample_count, &format) < 0) {
        if(!buf)
            errno = EINVAL;
        return SFXHND_INVALID;
    }

    effect = malloc(sizeof(snd_effect_t));

    if(effect == NULL) {
        return SFXHND_INVALID;
    }

    memset(effect, 0, sizeof(snd_effect_t));

    effect->rate = rate;
    effect->stereo = channels == 2;
    effect->fmt = format;
    effect->len = sample_count;

    effect->locl = snd_mem_malloc(chan_len);

    if(!effect->locl) {
        goto err_occurred;
    }

    allocation_size = __align_up(chan_len, 32);

    if(chan_len > 0) {
        tmp_buff = aligned_alloc(32, allocation_size);
        if(!tmp_buff)
            goto err_occurred;

        memset(tmp_buff, 0, allocation_size);
        memcpy(tmp_buff, buf, chan_len);
        bufidx += chan_len;

        spu_memload_sq(effect->locl, tmp_buff, chan_len);
    }

    if(channels > 1) {
        effect->locr = snd_mem_malloc(chan_len);

        if(!effect->locr) {
            goto err_occurred;
        }

        if(chan_len > 0) {
            memset(tmp_buff, 0, allocation_size);
            memcpy(tmp_buff, buf + bufidx, chan_len);
            bufidx += chan_len;
            spu_memload_sq(effect->locr, tmp_buff, chan_len);
        }
    }

    if(tmp_buff) {
        free(tmp_buff);
    }

    LIST_INSERT_HEAD(&snd_effects, effect, list);
    return (sfxhnd_t)effect;

err_occurred:
    if(effect->locl)
        snd_mem_free(effect->locl);
    if(effect->locr)
        snd_mem_free(effect->locr);
    if(tmp_buff)
        free(tmp_buff);

    free(effect);
    return SFXHND_INVALID;
}

int snd_sfx_play_chn(int chn, sfxhnd_t idx, int vol, int pan) {
    sfx_play_data_t data = {0};
    data.chn = chn;
    data.idx = idx;
    data.vol = vol;
    data.pan = pan;
    return snd_sfx_play_ex(&data);
}

static int find_free_channel(bool stereo) {
    int chn, checked, old;

    old = irq_disable();
    chn = sfx_nextchan;

    for(checked = 0; checked < 64; ++checked) {
        if(!(sfx_inuse & (1ULL << chn)) &&
           (!stereo || (chn < 63 && !(sfx_inuse & (1ULL << (chn + 1))))))
            break;

        chn = (chn + 1) % 64;
    }

    irq_restore(old);

    if(checked == 64) {
        errno = ENOSPC;
        return -1;
    }

    sfx_nextchan = (chn + (stereo ? 2 : 1)) % 64;
    return chn;
}

int snd_sfx_play(sfxhnd_t idx, int vol, int pan) {
    return snd_sfx_play_chn(-1, idx, vol, pan);
}

int snd_sfx_play_ex(sfx_play_data_t *data) {
    snd_effect_t *t;
    int result = 0;

    if(!data || !(t = find_snd_effect(data->idx)) || data->vol < 0 ||
       data->vol > 255 || data->pan < 0 || data->pan > 255 ||
       data->freq < 0 ||
       (data->loop &&
        (data->loopstart >= t->len ||
         (data->loopend &&
          (data->loopend > t->len || data->loopend <= data->loopstart))))) {
        errno = EINVAL;
        return -1;
    }

    if(data->chn < 0) {
        data->chn = find_free_channel(t->stereo);
        if(data->chn < 0) {
            return -1;
        }
    }
    else if(data->chn >= 64 || (t->stereo && data->chn >= 63)) {
        errno = EINVAL;
        return -1;
    }

    uint32_t size;
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);

    size = t->len;

    if(size >= 65535) size = 65534;

    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = data->chn;
    chan->cmd = AICA_CH_CMD_START;
    chan->base = t->locl;
    chan->type = t->fmt;
    chan->length = size;
    chan->loop = data->loop;
    chan->loopstart = data->loopstart;
    chan->loopend = data->loopend ? data->loopend : size;
    chan->freq = data->freq > 0 ? (uint32_t)data->freq : t->rate;
    chan->vol = data->vol;

    if(!t->stereo) {
        chan->pan = data->pan;
        result = snd_sh4_to_aica(tmp, cmd->size);
    }
    else {
        chan->pan = 0;

        snd_sh4_to_aica_stop();
        result = snd_sh4_to_aica(tmp, cmd->size);

        cmd->cmd_id = data->chn + 1;
        chan->base = t->locr;
        chan->pan = 255;
        if(!result)
            result = snd_sh4_to_aica(tmp, cmd->size);
        snd_sh4_to_aica_start();
    }

    return result < 0 ? -1 : data->chn;
}

void snd_sfx_stop(int chn) {
    AICA_CMDSTR_CHANNEL(tmp, cmd, chan);

    if(chn < 0 || chn >= 64) {
        errno = EINVAL;
        return;
    }

    cmd->cmd = AICA_CMD_CHAN;
    cmd->timestamp = 0;
    cmd->size = AICA_CMDSTR_CHANNEL_SIZE;
    cmd->cmd_id = chn;
    chan->cmd = AICA_CH_CMD_STOP;
    chan->base = 0;
    chan->type = 0;
    chan->length = 0;
    chan->loop = 0;
    chan->loopstart = 0;
    chan->loopend = 0;
    chan->freq = 44100;
    chan->vol = 0;
    chan->pan = 0;
    snd_sh4_to_aica(tmp, cmd->size);
}

void snd_sfx_stop_all(void) {
    int i;

    for(i = 0; i < 64; i++) {
        if(sfx_inuse & (1ULL << i))
            continue;

        snd_sfx_stop(i);
    }
}

int snd_sfx_chn_alloc(void) {
    int old, chn;

    old = irq_disable();

    for(chn = 0; chn < 64; chn++)
        if(!(sfx_inuse & (1ULL << chn)))
            break;

    if(chn >= 64) {
        chn = -1;
        errno = ENOSPC;
    }
    else {
        sfx_inuse |= 1ULL << chn;
    }

    irq_restore(old);

    return chn;
}

void snd_sfx_chn_free(int chn) {
    int old;

    if(chn < 0 || chn >= 64) {
        errno = EINVAL;
        return;
    }

    old = irq_disable();
    sfx_inuse &= ~(1ULL << chn);
    irq_restore(old);
}
