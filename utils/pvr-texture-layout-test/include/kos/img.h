#ifndef __KOS_IMG_H
#define __KOS_IMG_H

#include <stddef.h>
#include <stdint.h>

typedef struct kos_img {
    uint32_t w;
    uint32_t h;
    uint32_t fmt;
    size_t byte_count;
    void *data;
} kos_img_t;

#endif
