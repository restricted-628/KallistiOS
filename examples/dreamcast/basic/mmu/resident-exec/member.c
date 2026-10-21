/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black */
#include <stdint.h>
typedef uint32_t (*service_t)(uint32_t, uint32_t);
static volatile uint32_t marker = MEMBER_ID * 1000;
static volatile uint32_t counter;
#define PAGE(n) __attribute__((noinline, section(".text.page" #n)))
static uint32_t PAGE(4) page4(service_t service) {
    return service(MEMBER_ID, counter) + 4;
}
static uint32_t PAGE(3) page3(service_t service) { return page4(service) + 3; }
static uint32_t PAGE(2) page2(service_t service) { return page3(service) + 2; }
static uint32_t PAGE(1) page1(service_t service) { return page2(service) + 1; }
uint32_t PAGE(0) member_entry(service_t service) {
    ++counter;
    return marker + counter + page1(service);
}
