/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include "member-api.h"
#ifndef MEMBER_ID
#error Define MEMBER_ID
#endif
#define PAGE(n) __attribute__((section(".text.page" #n), noinline, noclone))
static volatile unsigned char payload[16384] = {[0 ... 16383] = MEMBER_ID};
static volatile unsigned char zeros[8196];
static volatile unsigned visits;
static PAGE(4) int page4(member_context_t *c) {
    return c->service(MEMBER_ID, visits, c->mode) + 4;
}
static PAGE(3) int page3(member_context_t *c) { return page4(c) + 3; }
static PAGE(2) int page2(member_context_t *c) { return page3(c) + 2; }
static PAGE(1) int page1(member_context_t *c) { return page2(c) + 1; }
PAGE(0) int overlay_entry(void *opaque) {
    member_context_t *c = opaque;
    if(c->cookie != MEMBER_COOKIE) return -1;
    for(unsigned i = 0; i < sizeof(payload); ++i)
        if(payload[i] != MEMBER_ID) return -2;
    for(unsigned i = 0; i < sizeof(zeros); ++i)
        if(zeros[i]) return -3;
    ++visits;
    int result = page1(c);
    if(c->cookie != MEMBER_COOKIE) return -4;
    return MEMBER_ID * 1000 + visits + result;
}
