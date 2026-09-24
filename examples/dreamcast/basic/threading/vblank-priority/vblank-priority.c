/* KallistiOS ##version##
   Copyright (C) 2026 Joseph Black
*/
#include <kos.h>
#include <dc/vblank.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static volatile bool armed;
static volatile unsigned int count;
static volatile uint8_t order[13];
static volatile int removal = -1;
static int self;

static void record(uint32_t code, void *data) {
    unsigned int value = (unsigned int)(uintptr_t)data;
    (void)code;
    if(!armed) return;
    if(count < sizeof(order)) order[count++] = value;
    if(value == 3) removal = vblank_handler_remove(self);
}

int main(void) {
    /* Interleaved APIs: three explicit ties, two legacy FIFO registrations. */
    static const uint8_t expected[] = { 1, 4, 3, 2, 5, 6, 7,
                                       1, 4, 2, 5, 6, 7 };
    int ids[7];
    int failed = 0;
    ids[0] = vblank_handler_add_prio(record, (void *)1, 0);
    ids[1] = vblank_handler_add_prio(record, (void *)2, 128);
    ids[2] = vblank_handler_add(record, (void *)5);
    ids[3] = self = vblank_handler_add_prio(record, (void *)3, 128);
    ids[4] = vblank_handler_add(record, (void *)6);
    ids[5] = vblank_handler_add_prio(record, (void *)4, 128);
    ids[6] = vblank_handler_add_prio(record, (void *)7, 255);
    for(unsigned int i = 0; i < 7; ++i)
        if(ids[i] < 0) failed = 1;
    /* No partial-registration frame can enter the recorded sequence. */
    armed = !failed;
    uint64_t deadline = timer_ms_gettime64() + 2000;
    while(count < sizeof(order) && timer_ms_gettime64() < deadline)
        thd_sleep(1);
    armed = false;
    if(count != sizeof(order) || removal != 0) failed = 1;
    for(unsigned int i = 0; i < sizeof(order); ++i)
        if(order[i] != expected[i]) failed = 1;
    for(unsigned int i = 0; i < 7; ++i)
        if(i != 3 && ids[i] >= 0 && vblank_handler_remove(ids[i]) < 0)
            failed = 1;
    printf("VBLANK-PRIORITY: %s events=%u ties=432 legacy=56 removal=%d\n",
           failed ? "FAIL" : "PASS", count, removal);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
