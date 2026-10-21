/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Original example's resident service table, not a dynamic import ABI. */
#ifndef OVERLAY_MEMBER_API_H
#define OVERLAY_MEMBER_API_H
#include <stdint.h>
typedef struct member_context {
    int (*yield_to_resident)(void);
    uint32_t cookie;
    int suspend;
} member_context_t;
#define MEMBER_COOKIE 0x44436f40u
#endif
