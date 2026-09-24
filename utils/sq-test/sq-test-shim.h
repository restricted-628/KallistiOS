/* Host substitutes for SQ ownership tests; no hardware behavior is simulated. */
#ifndef SQ_TEST_SHIM_H
#define SQ_TEST_SHIM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#define __is_aligned(p, n) (((uintptr_t)(p) & ((n) - 1)) == 0)
#define __predict_false(x) (x)
#define __noinline
#define MEM_AREA_SQ_BASE 0xe0000000u
#define DBG_ERROR 1
#define DBG_WARNING 2
#define DBG_DEAD 3
#define dbglog(...) ((void)0)
extern unsigned assertion_failures;
#define assert_msg(c, msg) ((c) ? (void)0 : (void)++assertion_failures)
typedef struct { int id; } kthread_t;
extern kthread_t *thd_current;
typedef struct { unsigned count; kthread_t *holder; } mutex_t;
#define RECURSIVE_MUTEX_INITIALIZER {0, NULL}
int mutex_lock(mutex_t *m);
int mutex_unlock(mutex_t *m);
bool irq_inside_int(void);
bool mmu_enabled(void);
void mmu_set_sq_addr(void *p);
void sq_flush(void *p);
void *sq_fast_cpy(void *dest, const void *src, size_t lines);
void *sq_set32(void *dest, uint32_t c, size_t n);
static inline void dcache_pref_line(const void *p) { (void)p; }
#endif
