/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include "member-api.h"
#ifndef MEMBER_ID
#error Define MEMBER_ID to 1 or 2
#endif
static volatile unsigned visits;
static volatile unsigned identity = MEMBER_ID;

int overlay_entry(void *opaque) {
    member_context_t *context = opaque;
    if(context->cookie != MEMBER_COOKIE) return -1;
    ++visits;
    if(context->suspend && context->yield_to_resident() < 0) return -2;
    if(context->cookie != MEMBER_COOKIE) return -3;
    return (int)(identity * 100 + visits);
}
