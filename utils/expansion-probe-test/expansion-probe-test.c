/* KallistiOS ##version##

   Expansion capability discovery host tests
   Copyright (C) 2026 Joseph Black
*/

#include <dc/asic.h>
#include <dc/expansion.h>
#include <kos/irq.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int mock_bba_present;
static int mock_la_result;
static int mock_modem_result;
static int mock_modem_initialized;
static int mock_la_initialized;
static int mock_8bit_active;
static int mock_pci_active;
static int la_probe_calls;
static int modem_probe_calls;

irq_mask_t irq_disable(void) {
    return 0;
}

void irq_restore(irq_mask_t state) {
    (void)state;
}

int asic_evt_get_status(uint16_t code, asic_evt_status_t *status) {
    memset(status, 0, sizeof(*status));
    status->code = code;
    status->handler_present = code == ASIC_EVT_EXP_8BIT
        ? mock_8bit_active : mock_pci_active;
    return 0;
}

int bba_probe(void) {
    return mock_bba_present;
}

int la_probe(void) {
    ++la_probe_calls;

    if(mock_la_result < 0)
        errno = EIO;

    return mock_la_result;
}

int la_is_initialized(void) {
    return mock_la_initialized;
}

int modem_probe(void) {
    ++modem_probe_calls;
    return mock_modem_result;
}

int modem_is_initialized(void) {
    return mock_modem_initialized;
}

static void reset_mocks(void) {
    mock_bba_present = 0;
    mock_la_result = 0;
    mock_modem_result = 0;
    mock_modem_initialized = 0;
    mock_la_initialized = 0;
    mock_8bit_active = 0;
    mock_pci_active = 0;
    la_probe_calls = 0;
    modem_probe_calls = 0;
}

static void test_default_probe(void) {
    expansion_status_t status;

    reset_mocks();
    assert(expansion_probe(&status, EXPANSION_PROBE_DEFAULT) == 0);
    assert(status.type == EXPANSION_DEVICE_NONE);
    assert(!status.complete);
    assert(!status.reset_performed);
    assert(!la_probe_calls && !modem_probe_calls);

    mock_bba_present = 1;
    assert(expansion_probe(&status, EXPANSION_PROBE_DEFAULT) == 0);
    assert(status.type == EXPANSION_DEVICE_BROADBAND);
    assert(status.present && status.complete);
    assert(status.capabilities & EXPANSION_CAP_DMA);
    assert(status.maximum_bps == 100000000u);
}

static void test_active_8bit_classification(void) {
    expansion_status_t status;

    reset_mocks();
    mock_8bit_active = 1;
    mock_modem_initialized = 1;
    assert(expansion_probe(&status, EXPANSION_PROBE_DEFAULT) == 0);
    assert(status.type == EXPANSION_DEVICE_MODEM);
    assert(status.active && status.complete);

    mock_modem_initialized = 0;
    mock_la_initialized = 1;
    assert(expansion_probe(&status, EXPANSION_PROBE_DEFAULT) == 0);
    assert(status.type == EXPANSION_DEVICE_LAN);

    mock_la_initialized = 0;
    assert(expansion_probe(&status, EXPANSION_PROBE_DEFAULT) == 0);
    assert(status.type == EXPANSION_DEVICE_UNKNOWN_8BIT);

    errno = 0;
    assert(expansion_probe(&status, EXPANSION_PROBE_RESET_8BIT) == -1);
    assert(errno == EBUSY && status.probe_error == EBUSY);
    assert(!la_probe_calls && !modem_probe_calls);
}

static void test_reset_probe(void) {
    expansion_status_t status;

    reset_mocks();
    mock_la_result = 1;
    assert(expansion_probe(&status, EXPANSION_PROBE_RESET_8BIT) == 0);
    assert(status.type == EXPANSION_DEVICE_LAN);
    assert(status.reset_performed && status.complete);
    assert(la_probe_calls == 1 && modem_probe_calls == 0);

    reset_mocks();
    mock_modem_result = 1;
    assert(expansion_probe(&status, EXPANSION_PROBE_RESET_8BIT) == 0);
    assert(status.type == EXPANSION_DEVICE_MODEM);
    assert(la_probe_calls == 1 && modem_probe_calls == 1);

    reset_mocks();
    assert(expansion_probe(&status, EXPANSION_PROBE_RESET_8BIT) == 0);
    assert(status.type == EXPANSION_DEVICE_NONE && status.complete);

    reset_mocks();
    mock_la_result = -1;
    assert(expansion_probe(&status, EXPANSION_PROBE_RESET_8BIT) == -1);
    assert(status.probe_error == EIO);
}

static void test_validation(void) {
    expansion_status_t status;

    errno = 0;
    assert(expansion_probe(NULL, 0) == -1 && errno == EINVAL);
    errno = 0;
    assert(expansion_probe(&status, 2) == -1 && errno == EINVAL);
}

int main(void) {
    test_validation();
    test_default_probe();
    test_active_8bit_classification();
    test_reset_probe();
    puts("expansion-probe-test: all tests passed");
    return 0;
}
