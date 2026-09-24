/* KallistiOS ##version##

   vmu_pkg_codec.c
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dc/vmu_pkg.h>

/* Kept as the shared KOS implementation rather than a private checksum copy.
   Declaring the narrow dependency here keeps this metadata codec usable by
   host image tools without importing target-only networking headers. */
uint16_t net_crc16ccitt(const uint8_t *data, int size, uint16_t start);

#define VMU_PKG_ICON_BYTES 512u

_Static_assert(sizeof(vmu_hdr_t) == 128,
               "vmu_hdr_t must describe the fixed package header");

static size_t bounded_length(const char *text, size_t maximum) {
    size_t length = 0;

    while(length < maximum && text[length])
        ++length;

    return length;
}

static int vmu_eyecatch_size(int eyecatch_type) {
    switch(eyecatch_type) {
        case VMUPKG_EC_NONE:
            return 0;
        case VMUPKG_EC_16BIT:
            return 72 * 56 * 2;
        case VMUPKG_EC_256COL:
            return 512 + 72 * 56;
        case VMUPKG_EC_16COL:
            return 32 + 72 * 56 / 2;
        default:
            return -1;
    }
}

int vmu_pkg_build(vmu_pkg_t *src, uint8_t **dst, int *dst_size) {
    uint8_t *out, *cursor;
    int ec_size;
    size_t icon_size, out_size;
    vmu_hdr_t *hdr;

    if(dst)
        *dst = NULL;
    if(dst_size)
        *dst_size = 0;

    if(!src || !dst || !dst_size || src->data_len < 0 ||
       src->icon_cnt < 0 || src->icon_cnt > 3 ||
       src->icon_anim_speed < 0 || src->icon_anim_speed > UINT16_MAX ||
       src->eyecatch_type < VMUPKG_EC_NONE ||
       src->eyecatch_type > VMUPKG_EC_16COL) {
        errno = EINVAL;
        return -2;
    }

    if((src->icon_cnt && !src->icon_data) ||
       (src->data_len && !src->data)) {
        errno = EINVAL;
        return -2;
    }

    icon_size = VMU_PKG_ICON_BYTES * (size_t)src->icon_cnt;
    ec_size = vmu_eyecatch_size(src->eyecatch_type);
    if(ec_size < 0 || (ec_size && !src->eyecatch_data)) {
        errno = EINVAL;
        return -1;
    }

    if(icon_size > SIZE_MAX - sizeof(vmu_hdr_t) ||
       (size_t)ec_size > SIZE_MAX - sizeof(vmu_hdr_t) - icon_size ||
       (size_t)src->data_len > SIZE_MAX - sizeof(vmu_hdr_t) - icon_size -
                               (size_t)ec_size) {
        errno = EOVERFLOW;
        return -1;
    }

    out_size = sizeof(vmu_hdr_t) + icon_size + (size_t)ec_size +
               (size_t)src->data_len;
    if(out_size > INT_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    out = malloc(out_size);
    if(!out) {
        errno = ENOMEM;
        return -1;
    }

    *dst = out;
    *dst_size = (int)out_size;
    memset(out, 0, out_size);
    hdr = (vmu_hdr_t *)out;
    memset(hdr->desc_short, ' ', sizeof(hdr->desc_short));
    memset(hdr->desc_long, ' ', sizeof(hdr->desc_long));

    memcpy(hdr->desc_short, src->desc_short,
           bounded_length(src->desc_short, sizeof(hdr->desc_short)));
    memcpy(hdr->desc_long, src->desc_long,
           bounded_length(src->desc_long, sizeof(hdr->desc_long)));
    memcpy(hdr->app_id, src->app_id,
           bounded_length(src->app_id, sizeof(hdr->app_id)));
    hdr->icon_cnt = (uint16_t)src->icon_cnt;
    hdr->icon_anim_speed = (uint16_t)src->icon_anim_speed;
    hdr->eyecatch_type = (uint16_t)src->eyecatch_type;
    hdr->data_len = (uint32_t)src->data_len;
    memcpy(hdr->icon_pal, src->icon_pal, sizeof(hdr->icon_pal));

    cursor = out + sizeof(vmu_hdr_t);
    if(icon_size)
        memcpy(cursor, src->icon_data, icon_size);
    cursor += icon_size;
    if(ec_size)
        memcpy(cursor, src->eyecatch_data, (size_t)ec_size);
    cursor += ec_size;
    if(src->data_len)
        memcpy(cursor, src->data, (size_t)src->data_len);
    cursor += src->data_len;

    if((size_t)(cursor - out) != out_size) {
        free(out);
        *dst = NULL;
        *dst_size = 0;
        errno = EFAULT;
        return -1;
    }
    hdr->crc = net_crc16ccitt(out, (int)out_size, 0);
    return 0;
}

int vmu_pkg_parse(uint8_t *data, size_t data_size, vmu_pkg_t *pkg) {
    static const uint8_t zero_crc[sizeof(uint16_t)] = {0};
    uint16_t crc, crc_save;
    int ec_size;
    size_t hdr_size, total_size, icon_size, crc_offset;
    vmu_hdr_t header;

    if(pkg)
        memset(pkg, 0, sizeof(*pkg));

    if(!data || !pkg || data_size < sizeof(vmu_hdr_t)) {
        errno = EINVAL;
        return -1;
    }

    /* The package can begin at any byte alignment. Copying the fixed header
       also prevents checksum validation from modifying read-only input. */
    memcpy(&header, data, sizeof(header));

    if(header.icon_cnt > 3) {
        errno = EILSEQ;
        return -1;
    }

    icon_size = VMU_PKG_ICON_BYTES * (size_t)header.icon_cnt;
    ec_size = vmu_eyecatch_size(header.eyecatch_type);
    if(ec_size < 0 || icon_size > SIZE_MAX - sizeof(vmu_hdr_t) ||
       (size_t)ec_size > SIZE_MAX - sizeof(vmu_hdr_t) - icon_size) {
        errno = EILSEQ;
        return -1;
    }

    hdr_size = sizeof(vmu_hdr_t) + icon_size + (size_t)ec_size;
    if((size_t)header.data_len > SIZE_MAX - hdr_size) {
        errno = EILSEQ;
        return -1;
    }

    total_size = hdr_size + (size_t)header.data_len;
    if(total_size > data_size) {
        errno = EILSEQ;
        return -1;
    }
    if(total_size > INT_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    /* Calculate the checksum as if the encoded CRC field were zero, chaining
       around that field rather than temporarily rewriting the caller's data. */
    crc_save = header.crc;
    crc_offset = offsetof(vmu_hdr_t, crc);
    crc = net_crc16ccitt(data, (int)crc_offset, 0);
    crc = net_crc16ccitt(zero_crc, sizeof(zero_crc), crc);
    crc = net_crc16ccitt(data + crc_offset + sizeof(uint16_t),
                         (int)(total_size - crc_offset - sizeof(uint16_t)),
                         crc);

    if(crc_save != crc) {
        errno = EILSEQ;
        return -1;
    }

    pkg->icon_cnt = header.icon_cnt;
    pkg->icon_anim_speed = header.icon_anim_speed;
    pkg->eyecatch_type = header.eyecatch_type;
    pkg->data_len = (int)header.data_len;
    memcpy(pkg->icon_pal, header.icon_pal, sizeof(header.icon_pal));
    pkg->icon_data = data + sizeof(vmu_hdr_t);
    pkg->eyecatch_data = data + sizeof(vmu_hdr_t) + icon_size;
    pkg->data = data + hdr_size;
    memcpy(pkg->desc_short, header.desc_short, sizeof(header.desc_short));
    memcpy(pkg->desc_long, header.desc_long, sizeof(header.desc_long));
    memcpy(pkg->app_id, header.app_id, sizeof(header.app_id));
    pkg->desc_short[sizeof(header.desc_short)] = '\0';
    pkg->desc_long[sizeof(header.desc_long)] = '\0';
    pkg->app_id[sizeof(header.app_id)] = '\0';

    return 0;
}
