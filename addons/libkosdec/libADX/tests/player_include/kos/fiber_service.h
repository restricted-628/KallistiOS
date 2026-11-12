/* Host ownership fixture, not a KOS runtime/ABI implementation. */
#ifndef TEST_PLAYER_FIBER_H
#define TEST_PLAYER_FIBER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct fiber_service fiber_service_t;
typedef struct fiber_service_executor fiber_service_executor_t;
typedef void (*fiber_service_entry_t)(fiber_service_t *,void *);
fiber_service_executor_t *fiber_service_executor_create(void);
fiber_service_t *fiber_service_add(fiber_service_executor_t *,void *,size_t,fiber_service_entry_t,void *);
int fiber_service_executor_start(fiber_service_executor_t *,const void *);
int fiber_service_executor_destroy(fiber_service_executor_t *);
int fiber_service_wake(fiber_service_t *);
int fiber_service_request_stop(fiber_service_t *);
bool fiber_service_stop_requested(fiber_service_t *);
int fiber_service_wait(fiber_service_t *,uint64_t);
int fiber_service_yield(fiber_service_t *);
#endif
