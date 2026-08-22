#ifndef __RTC_TEST_DC_G2BUS_H
#define __RTC_TEST_DC_G2BUS_H

#include <stdint.h>

typedef struct {
    unsigned int state;
} g2_ctx_t;

g2_ctx_t g2_lock(void);
void g2_unlock(g2_ctx_t context);
uint32_t g2_read_32_raw(uintptr_t address);
void g2_write_32_raw(uintptr_t address, uint32_t value);
void g2_fifo_wait(void);

#endif
