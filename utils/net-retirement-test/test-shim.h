/* Original control-flow regression boundary; no device timing is modeled. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
typedef unsigned irq_mask_t;
static irq_mask_t mask;
static bool in_irq;
static irq_mask_t irq_disable(void) { irq_mask_t old=mask; mask=0xf0; return old; }
static void irq_restore(irq_mask_t old) { mask=old; }
static __attribute__((unused)) void scoped_restore(irq_mask_t *old) { irq_restore(*old); }
#define irq_disable_scoped() irq_mask_t saved_irq __attribute__((cleanup(scoped_restore))) = irq_disable()
static __attribute__((unused)) bool irq_inside_int(void) { return in_irq; }
typedef struct { int state; } kthread_t;
#define STATE_RUNNING 1
static kthread_t main_thread={STATE_RUNNING}, worker_thread={STATE_RUNNING};
static kthread_t *current=&main_thread;
static kthread_t *thd_get_current(void) { return current; }
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); assert(x); } } while(0)
