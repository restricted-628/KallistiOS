#ifndef TMU_TEST_SHIM_H
#define TMU_TEST_SHIM_H
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <kos/irq.h>
static uint8_t test_regs8[0x30];
static uint16_t test_regs16[0x30];
static uint32_t test_regs32[0x30];
static unsigned masked, irq_context, priorities[4], allocated, freed;
static bool fail_alloc;
static irq_cb_t handlers[3];
static void *test_calloc(size_t, size_t);
static void test_free(void *);
#define calloc test_calloc
#define free test_free
#define assert_msg(test, message) assert(test)
#endif
