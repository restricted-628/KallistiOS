/* KallistiOS ##version##

   scif-config-test.c
   Copyright (C) 2026 Joseph Black
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "scif_config_internal.h"

static int failures;

#define CHECK(condition) do { \
    if(!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        ++failures; \
    } \
} while(0)

static scif_config_t config_8n1(uint32_t baud) {
    scif_config_t config = {
        .baud = baud,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = SCIF_PARITY_NONE,
        .flow_control = SCIF_FLOW_NONE,
        .rx_trigger = SCIF_RX_TRIGGER_4,
        .tx_trigger = SCIF_TX_TRIGGER_8
    };

    return config;
}

static void test_baud_selection(void) {
    scif_register_config_t registers;
    scif_config_t config = config_8n1(115200);

    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.bit_rate == 13);
    CHECK(registers.actual_baud == 111607);
    CHECK(registers.baud_error_ppm < 0);
    CHECK(registers.mode == 0);

    config.baud = 230400;
    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.bit_rate == 6);
    CHECK(registers.actual_baud == 223214);

    config.baud = 1562500;
    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.bit_rate == 0);
    CHECK(registers.actual_baud == 1562500);
    CHECK(registers.baud_error_ppm == 0);

    config.baud = 9600;
    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.bit_rate == 162);
    CHECK(registers.actual_baud == 9585);
}

static void test_format_and_triggers(void) {
    scif_register_config_t registers;
    scif_config_t config = config_8n1(0);

    config.data_bits = 7;
    config.stop_bits = 2;
    config.parity = SCIF_PARITY_ODD;
    config.flow_control = SCIF_FLOW_HARDWARE;
    config.rx_trigger = SCIF_RX_TRIGGER_14;
    config.tx_trigger = SCIF_TX_TRIGGER_1;

    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.mode == 0x78);
    CHECK(registers.fifo == 0xf8);
    CHECK(registers.control_clock == 0x02);
    CHECK(registers.actual_baud == 0);

    config.data_bits = 8;
    config.stop_bits = 1;
    config.parity = SCIF_PARITY_EVEN;
    config.flow_control = SCIF_FLOW_NONE;
    config.rx_trigger = SCIF_RX_TRIGGER_1;
    config.tx_trigger = SCIF_TX_TRIGGER_8;
    CHECK(scif_config_encode(&config, &registers) == 0);
    CHECK(registers.mode == 0x20);
    CHECK(registers.fifo == 0);
}

static void expect_invalid(scif_config_t *config, int expected_errno) {
    scif_register_config_t registers;

    errno = 0;
    CHECK(scif_config_encode(config, &registers) < 0);
    CHECK(errno == expected_errno);
}

static void test_invalid_configurations(void) {
    scif_register_config_t registers;
    scif_config_t config = config_8n1(115200);

    errno = 0;
    CHECK(scif_config_encode(NULL, &registers) < 0);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(scif_config_encode(&config, NULL) < 0);
    CHECK(errno == EINVAL);

    config.data_bits = 6;
    expect_invalid(&config, EINVAL);
    config = config_8n1(115200);
    config.stop_bits = 3;
    expect_invalid(&config, EINVAL);
    config = config_8n1(115200);
    config.parity = (scif_parity_t)99;
    expect_invalid(&config, EINVAL);
    config = config_8n1(115200);
    config.flow_control = (scif_flow_control_t)99;
    expect_invalid(&config, EINVAL);
    config = config_8n1(115200);
    config.rx_trigger = (scif_rx_trigger_t)2;
    expect_invalid(&config, EINVAL);
    config = config_8n1(115200);
    config.tx_trigger = (scif_tx_trigger_t)3;
    expect_invalid(&config, EINVAL);
    config = config_8n1(UINT32_MAX);
    expect_invalid(&config, ERANGE);
    config = config_8n1(1);
    expect_invalid(&config, ERANGE);
}

int main(void) {
    test_baud_selection();
    test_format_and_triggers();
    test_invalid_configurations();

    if(failures) {
        fprintf(stderr, "%d SCIF configuration test(s) failed\n", failures);
        return 1;
    }

    puts("SCIF configuration tests passed");
    return 0;
}
