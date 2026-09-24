#ifndef TEST_FIBER_SERVICE_H
#define TEST_FIBER_SERVICE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct fiber_service {
    bool stop;
    unsigned yields, stop_after;
    void (*entry)(struct fiber_service *, void *);
    void *data;
} fiber_service_t;
typedef struct fiber_service_executor { fiber_service_t service; }
    fiber_service_executor_t;
fiber_service_t *fiber_service_add(fiber_service_executor_t *, void *, size_t,
                                  void (*)(fiber_service_t *, void *), void *);
void *fiber_service_executor_get_thread(fiber_service_executor_t *);
bool fiber_service_stop_requested(fiber_service_t *);
int fiber_service_wait(fiber_service_t *, uint64_t);
int fiber_service_yield(fiber_service_t *);
int fiber_service_wake(fiber_service_t *);
void thd_pass(void);
#endif
