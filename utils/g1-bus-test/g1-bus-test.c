#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arch/arch.h>
#include <dc/asic.h>
#include <kos/irq.h>
#include <kos/sem.h>

#include "g1_bus.h"

#define CLAIM_CAPACITY 5

typedef struct fake_claim {
    uint16_t code;
    asic_evt_handler handler;
    void *data;
    bool active;
    bool masked;
} fake_claim_t;

static fake_claim_t claims[CLAIM_CAPACITY];
static uint8_t taskfile[0x22];
static uint32_t dma_enable;
static uint32_t dma_status;
static unsigned int irq_depth;
static bool inside_irq;
static int system_mode = HW_TYPE_RETAIL;
static uint64_t fake_time;
static unsigned int pass_count;
static unsigned int clear_dma_after_pass;
static int claim_failure_call = -1;
static unsigned int claim_calls;
static unsigned int release_calls;
static unsigned int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static size_t taskfile_index(uintptr_t address) {
    return (size_t)((address - G1_ATA_ALTSTATUS) >> 2);
}

uint8_t g1_test_in8(uintptr_t address) {
    if(address == G1_ATA_ALTSTATUS)
        return taskfile[0];
    if(address >= G1_ATA_ERROR && address <= G1_ATA_STATUS_REG)
        return taskfile[taskfile_index(address)];
    abort();
}

uint32_t g1_test_in32(uintptr_t address) {
    if(address == G1_ATA_DMA_ENABLE)
        return dma_enable;
    if(address == G1_ATA_DMA_STATUS)
        return dma_status;
    abort();
}

void g1_test_out8(uintptr_t address, uint8_t value) {
    if(address >= G1_ATA_FEATURES && address <= G1_ATA_COMMAND_REG) {
        taskfile[taskfile_index(address)] = value;
        return;
    }
    abort();
}

void g1_test_out32(uintptr_t address, uint32_t value) {
    if(address == G1_ATA_DMA_ENABLE) {
        dma_enable = value;
        return;
    }
    abort();
}

irq_mask_t irq_disable(void) {
    return irq_depth++;
}

void irq_restore(irq_mask_t state) {
    CHECK(irq_depth > 0);
    irq_depth = state;
}

bool irq_inside_int(void) {
    return inside_irq;
}

int hardware_sys_mode(int *region) {
    if(region)
        *region = 0;
    return system_mode;
}

int sem_wait(semaphore_t *semaphore) {
    CHECK(semaphore && semaphore->initialized);
    if(semaphore->count <= 0) {
        errno = EAGAIN;
        return -1;
    }
    --semaphore->count;
    return 0;
}

int sem_wait_timed(semaphore_t *semaphore, unsigned int timeout) {
    if(!timeout)
        return sem_wait(semaphore);
    if(sem_wait(semaphore) < 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    return 0;
}

int sem_trywait(semaphore_t *semaphore) {
    if(sem_wait(semaphore) < 0) {
        errno = EWOULDBLOCK;
        return -1;
    }
    return 0;
}

int sem_wait_irqsafe(semaphore_t *semaphore) {
    return inside_irq ? sem_trywait(semaphore) : sem_wait(semaphore);
}

int sem_signal(semaphore_t *semaphore) {
    CHECK(semaphore && semaphore->initialized);
    ++semaphore->count;
    return 0;
}

void thd_pass(void) {
    ++pass_count;
    if(clear_dma_after_pass && pass_count >= clear_dma_after_pass)
        dma_status = 0;
}

uint64_t timer_ms_gettime64(void) {
    return fake_time++;
}

int asic_evt_claim(uint16_t code, uint8_t irqlevel,
                   asic_evt_handler handler, void *data,
                   asic_evt_claim_t *claim) {
    unsigned int i;

    CHECK(irqlevel == ASIC_IRQB);
    if(claim)
        *claim = ASIC_EVT_CLAIM_INVALID;

    if((int)claim_calls++ == claim_failure_call) {
        errno = EBUSY;
        return -1;
    }

    for(i = 0; i < CLAIM_CAPACITY; ++i) {
        if(!claims[i].active)
            break;
    }

    if(i == CLAIM_CAPACITY || !handler || !claim) {
        errno = EBUSY;
        return -1;
    }

    claims[i] = (fake_claim_t) {
        .code = code,
        .handler = handler,
        .data = data,
        .active = true,
        .masked = false
    };
    *claim = i + 1;
    return 0;
}

static fake_claim_t *claim_for_token(asic_evt_claim_t claim) {
    if(!claim || claim > CLAIM_CAPACITY || !claims[claim - 1].active)
        return NULL;
    return &claims[claim - 1];
}

int asic_evt_claim_mask(asic_evt_claim_t claim) {
    fake_claim_t *entry = claim_for_token(claim);
    if(!entry) {
        errno = ENOENT;
        return -1;
    }
    entry->masked = true;
    return 0;
}

int asic_evt_claim_unmask(asic_evt_claim_t claim) {
    fake_claim_t *entry = claim_for_token(claim);
    if(!entry) {
        errno = ENOENT;
        return -1;
    }
    entry->masked = false;
    return 0;
}

int asic_evt_release(asic_evt_claim_t claim) {
    fake_claim_t *entry = claim_for_token(claim);
    if(!entry) {
        errno = ENOENT;
        return -1;
    }
    *entry = (fake_claim_t) { 0 };
    ++release_calls;
    return 0;
}

static fake_claim_t *claim_for_code(uint16_t code) {
    unsigned int i;

    for(i = 0; i < CLAIM_CAPACITY; ++i) {
        if(claims[i].active && claims[i].code == code)
            return &claims[i];
    }
    return NULL;
}

static void invoke_event(uint16_t code) {
    fake_claim_t *entry = claim_for_code(code);
    bool prior = inside_irq;

    CHECK(entry && !entry->masked);
    if(!entry || entry->masked)
        return;

    inside_irq = true;
    entry->handler(code, entry->data);
    inside_irq = prior;
}

static unsigned int client_calls[3];

static bool first_client(uint32_t code, void *data) {
    CHECK(code == ASIC_EVT_GD_DMA);
    ++client_calls[(uintptr_t)data];
    return false;
}

static bool handling_client(uint32_t code, void *data) {
    CHECK(code == ASIC_EVT_GD_DMA);
    ++client_calls[(uintptr_t)data];
    return true;
}

static bool command_client(uint32_t code, void *data) {
    unsigned int *calls = data;
    CHECK(code == ASIC_EVT_GD_COMMAND);
    ++*calls;
    return true;
}

static void test_locking_and_device_selection(void) {
    uint8_t previous = 0xff;

    taskfile[taskfile_index(G1_ATA_DEVICE_SELECT)] = 0;
    taskfile[0] = 0;
    CHECK(g1_bus_device_state_init() == 0);
    CHECK(g1_bus_lock() == 0);
    errno = 0;
    CHECK(g1_bus_trylock() < 0 && errno == EWOULDBLOCK);
    CHECK(g1_bus_unlock() == 0);

    CHECK(g1_bus_select_device_timed(G1_ATA_DEVICE_SLAVE_BIT, 5,
                                     &previous) == 0);
    CHECK(previous == 0);
    CHECK(taskfile[taskfile_index(G1_ATA_DEVICE_SELECT)]
          == G1_ATA_DEVICE_SLAVE_BIT);

    dma_status = 1;
    pass_count = 0;
    clear_dma_after_pass = 2;
    CHECK(g1_bus_select_device_timed(0, 8, &previous) == 0);
    CHECK(pass_count == 2);
    CHECK(previous == G1_ATA_DEVICE_SLAVE_BIT);
    clear_dma_after_pass = 0;

    taskfile[0] = G1_ATA_SR_BSY;
    fake_time = 0;
    errno = 0;
    CHECK(g1_bus_select_device_timed(G1_ATA_DEVICE_SLAVE_BIT, 3,
                                     NULL) < 0);
    CHECK(errno == ETIMEDOUT);

    inside_irq = true;
    errno = 0;
    CHECK(g1_bus_lock_timed(1) < 0 && errno == EPERM);
    CHECK(g1_bus_select_device(G1_ATA_DEVICE_SLAVE_BIT) == 0x0f);
    inside_irq = false;
    taskfile[0] = 0;
}

static void test_dma_dispatch_and_unwind(void) {
    g1_bus_dma_client_t client0;
    g1_bus_dma_client_t client1;
    unsigned int releases_before;

    claim_failure_call = (int)claim_calls + 2;
    releases_before = release_calls;
    errno = 0;
    CHECK(g1_bus_dma_client_register(first_client, (void *)0)
          == G1_BUS_DMA_CLIENT_INVALID);
    CHECK(errno == EBUSY);
    CHECK(release_calls == releases_before + 2);
    CHECK(!claim_for_code(ASIC_EVT_GD_DMA));

    claim_failure_call = -1;
    client0 = g1_bus_dma_client_register(first_client, (void *)0);
    client1 = g1_bus_dma_client_register(handling_client, (void *)1);
    CHECK(client0 != G1_BUS_DMA_CLIENT_INVALID);
    CHECK(client1 != G1_BUS_DMA_CLIENT_INVALID);
    CHECK(claim_for_code(ASIC_EVT_GD_DMA));
    CHECK(claim_for_code(ASIC_EVT_GD_DMA_OVERRUN));
    CHECK(claim_for_code(ASIC_EVT_GD_DMA_ILLADDR));
    CHECK(claim_for_code(ASIC_EVT_GD_DMA_ACCESS));

    invoke_event(ASIC_EVT_GD_DMA);
    CHECK(client_calls[0] == 1);
    CHECK(client_calls[1] == 1);

    CHECK(g1_bus_dma_client_unregister(client0) == 0);
    CHECK(claim_for_code(ASIC_EVT_GD_DMA));
    CHECK(g1_bus_dma_client_unregister(client1) == 0);
    CHECK(!claim_for_code(ASIC_EVT_GD_DMA));
    CHECK(!claim_for_code(ASIC_EVT_GD_DMA_ACCESS));
}

static void test_command_dispatch(void) {
    unsigned int calls = 0;
    fake_claim_t *entry;

    CHECK(g1_bus_gd_command_client_register(command_client, &calls) == 0);
    entry = claim_for_code(ASIC_EVT_GD_COMMAND);
    CHECK(entry != NULL);
    invoke_event(ASIC_EVT_GD_COMMAND);
    CHECK(calls == 1);
    CHECK(g1_bus_gd_command_client_mask() == 0);
    CHECK(entry->masked);
    CHECK(g1_bus_gd_command_client_unmask() == 0);
    CHECK(!entry->masked);
    CHECK(g1_bus_gd_command_client_unregister() == 0);
    CHECK(!claim_for_code(ASIC_EVT_GD_COMMAND));
}

static void test_dma_registers_and_fault_latch(void) {
    dma_status = 1;
    dma_enable = 1;
    CHECK(g1_bus_dma_in_progress());
    g1_bus_dma_disable();
    CHECK(dma_enable == 0);
    dma_status = 0;
    CHECK(!g1_bus_dma_in_progress());

    CHECK(g1_bus_lock() == 0);
    g1_bus_mark_faulted();
    CHECK(g1_bus_is_faulted());
    errno = 0;
    CHECK(g1_bus_unlock() < 0 && errno == EIO);
    errno = 0;
    CHECK(g1_bus_lock() < 0 && errno == EIO);
    errno = 0;
    CHECK(g1_bus_trylock() < 0 && errno == EIO);
    errno = 0;
    CHECK(g1_bus_lock_timed(1) < 0 && errno == EIO);
}

int main(void) {
    test_locking_and_device_selection();
    test_dma_dispatch_and_unwind();
    test_command_dispatch();
    test_dma_registers_and_fault_latch();

    CHECK(irq_depth == 0);

    if(failures) {
        fprintf(stderr, "%u shared G1 checks failed\n", failures);
        return EXIT_FAILURE;
    }

    puts("Shared G1 ownership tests passed");
    return EXIT_SUCCESS;
}
