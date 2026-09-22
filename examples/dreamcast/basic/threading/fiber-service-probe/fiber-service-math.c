/* KallistiOS ##version##

   fiber-service-math.c
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <kos/fiber_service.h>
#include <dc/matrix.h>

#include <errno.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MATH_ROUNDS 32u
#define MATH_TIMEOUT_MS 5000u

typedef struct math_probe {
    matrix_t expected;
    float cookie;
    volatile unsigned rounds;
    volatile bool waiting;
    volatile bool cancelled;
} math_probe_t;

static alignas(32) uint8_t math_stacks[2][8192];
static alignas(32) math_probe_t probes[2];
static semaphore_t helper_request, helper_reply;
static volatile bool helper_stop;
static volatile unsigned helper_runs;
static volatile bool math_failed;

static void check_matrix(const matrix_t *expected) {
    alignas(32) matrix_t observed;
    float x = 1.0f, y = 2.0f, z = 3.0f, w = 4.0f;
    float transformed[4];

    mat_store(&observed);
    if(memcmp(&observed, expected, sizeof(observed)))
        math_failed = true;

    /* Exercise the accelerator as well as checking all 16 stored registers.
       Small integer coefficients make these products/sums exactly representable. */
    mat_trans_nodiv(x, y, z, w);
    transformed[0] = x;
    transformed[1] = y;
    transformed[2] = z;
    transformed[3] = w;
    for(unsigned row = 0; row < 4; ++row) {
        float value = 0;

        for(unsigned col = 0; col < 4; ++col)
            value += (*expected)[col][row] * (float)(col + 1);
        if(transformed[row] != value)
            math_failed = true;
    }
}

static void *matrix_helper(void *data) {
    alignas(32) matrix_t poison;

    (void)data;
    for(unsigned col = 0; col < 4; ++col)
        for(unsigned row = 0; row < 4; ++row)
            poison[col][row] = -(float)(100 + col * 4 + row);

    for(;;) {
        if(sem_wait(&helper_request) < 0 || helper_stop)
            break;
        mat_load(&poison);
        ++helper_runs;
        sem_signal(&helper_reply);
    }
    return NULL;
}

static void math_entry(fiber_service_t *service, void *data) {
    math_probe_t *probe = data;
    register float scalar __asm__("fr12") = probe->cookie;
    alignas(32) matrix_t identity = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}
    };

    __asm__ volatile("" : "+f"(scalar));
    if(fiber_get_attach_flags() != KFIBER_ATTACH_MATH_CONTEXT)
        math_failed = true;
    check_matrix(&identity);
    mat_load(&probe->expected);

    for(unsigned round = 0; round < MATH_ROUNDS; ++round) {
        if(fiber_service_yield(service) < 0) {
            math_failed = true;
            return;
        }
        __asm__ volatile("" : "+f"(scalar));
        if(scalar != probe->cookie)
            math_failed = true;
        check_matrix(&probe->expected);

        /* Deliberately block the whole executor for this regression only:
           the other KOS thread must overwrite XMTRX before we resume. Normal
           service work should use cooperative synchronization instead. */
        sem_signal(&helper_request);
        if(sem_wait_timed(&helper_reply, MATH_TIMEOUT_MS) < 0) {
            math_failed = true;
            return;
        }
        check_matrix(&probe->expected);

        if(fiber_service_wait(service, timer_ms_gettime64() + 2) < 0) {
            math_failed = true;
            return;
        }
        __asm__ volatile("" : "+f"(scalar));
        if(scalar != probe->cookie)
            math_failed = true;
        check_matrix(&probe->expected);
        ++probe->rounds;
    }

    probe->waiting = true;
    if(fiber_service_wait(service, 0) < 0 && errno == ECANCELED) {
        __asm__ volatile("" : "+f"(scalar));
        if(scalar != probe->cookie)
            math_failed = true;
        check_matrix(&probe->expected);
        probe->cancelled = true;
    }
    else
        math_failed = true;
}

int test_service_math(void) {
    fiber_service_executor_t *executor = NULL;
    fiber_service_t *services[2];
    kthread_t *helper = NULL;
    uint64_t deadline;

    errno = 0;
    executor = fiber_service_executor_create_ex(~(unsigned)KFIBER_ATTACH_MATH_CONTEXT);
    if(executor || errno != EINVAL) {
        if(executor)
            fiber_service_executor_destroy(executor);
        return -1;
    }
    sem_init(&helper_request, 0);
    sem_init(&helper_reply, 0);
    executor = fiber_service_executor_create_ex(KFIBER_ATTACH_MATH_CONTEXT);
    if(!executor)
        goto fail;
    for(unsigned index = 0; index < 2; ++index) {
        for(unsigned col = 0; col < 4; ++col)
            for(unsigned row = 0; row < 4; ++row)
                probes[index].expected[col][row] = (float)(1 + index * 20 + col * 4 + row);
        probes[index].cookie = index ? -77.5f : 123.25f;
        services[index] = fiber_service_add(executor, math_stacks[index],
            sizeof(math_stacks[index]), math_entry, &probes[index]);
        if(!services[index])
            goto fail;
        fiber_service_wake(services[index]);
    }
    helper = thd_create(false, matrix_helper, NULL);
    if(!helper || fiber_service_executor_start(executor, NULL) < 0)
        goto fail;
    deadline = timer_ms_gettime64() + MATH_TIMEOUT_MS;
    while(timer_ms_gettime64() < deadline) {
        if(probes[0].waiting && probes[1].waiting &&
           fiber_service_get_state(services[0]) == FIBER_SERVICE_WAITING &&
           fiber_service_get_state(services[1]) == FIBER_SERVICE_WAITING)
            break;
        thd_pass();
    }
    if(!probes[0].waiting || !probes[1].waiting)
        math_failed = true;
    if(fiber_service_executor_destroy(executor) < 0)
        math_failed = true;
    executor = NULL;
    helper_stop = true;
    sem_signal(&helper_request);
    thd_join(helper, NULL);
    sem_destroy(&helper_request);
    sem_destroy(&helper_reply);

    if(probes[0].rounds != MATH_ROUNDS || probes[1].rounds != MATH_ROUNDS ||
       !probes[0].cancelled || !probes[1].cancelled ||
       helper_runs != 2 * MATH_ROUNDS)
        math_failed = true;
    printf("KOSFIBERSVCMATH rounds=%u/%u preempt=%u shutdown=%u/%u result=%s\n",
           probes[0].rounds, probes[1].rounds, helper_runs,
           probes[0].cancelled, probes[1].cancelled, math_failed ? "FAIL" : "PASS");
    return math_failed ? -1 : 0;

fail:
    if(executor)
        fiber_service_executor_destroy(executor);
    if(helper) {
        helper_stop = true;
        sem_signal(&helper_request);
        thd_join(helper, NULL);
    }
    sem_destroy(&helper_request);
    sem_destroy(&helper_reply);
    return -1;
}
