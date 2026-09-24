#ifndef TMU_TEST_IRQ_H
#define TMU_TEST_IRQ_H
#include <stdbool.h>
#include <stdint.h>
typedef unsigned irq_mask_t;
typedef unsigned irq_t;
typedef struct { unsigned unused; } irq_context_t;
typedef void (*irq_hdl_t)(irq_t, irq_context_t *, void *);
typedef struct { irq_hdl_t hdl; void *data; } irq_cb_t;
enum { IRQ_SRC_RTC, IRQ_SRC_TMU2, IRQ_SRC_TMU1, IRQ_SRC_TMU0 };
enum { EXC_TMU0_TUNI0 = 0x400, EXC_TMU1_TUNI1 = 0x420, EXC_TMU2_TUNI2 = 0x440 };
#define IRQ_PRIO_MASKED 0
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t);
bool irq_inside_int(void);
int irq_set_priority(unsigned, unsigned);
unsigned irq_get_priority(unsigned);
int irq_set_handler(irq_t, irq_hdl_t, void *);
irq_cb_t irq_get_handler(irq_t);
static inline void test_restore(irq_mask_t *state) { irq_restore(*state); }
#define irq_disable_scoped() \
    irq_mask_t test_saved_irq __attribute__((cleanup(test_restore))) = irq_disable()
#endif
