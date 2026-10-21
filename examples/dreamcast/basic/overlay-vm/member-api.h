/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
   Original fixture service table, not a general dynamic-import ABI. */
#ifndef OVERLAY_VM_MEMBER_API_H
#define OVERLAY_VM_MEMBER_API_H
#include <stdint.h>
#define MEMBER_COOKIE 0x44436f40u
#define MEMBER_BASE 0x101ff000u
#define MEMBER_PAGES 12u
typedef struct member_context {
    int (*service)(unsigned id, unsigned visit, unsigned mode);
    uint32_t cookie;
    unsigned mode;
} member_context_t;
#endif
