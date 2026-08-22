#ifndef G1_BUS_TEST_SHIM_H
#define G1_BUS_TEST_SHIM_H

#include <stdint.h>

uint8_t g1_test_in8(uintptr_t address);
uint32_t g1_test_in32(uintptr_t address);
void g1_test_out8(uintptr_t address, uint8_t value);
void g1_test_out32(uintptr_t address, uint32_t value);

#define G1_BUS_IN8(address) g1_test_in8((uintptr_t)(address))
#define G1_BUS_IN32(address) g1_test_in32((uintptr_t)(address))
#define G1_BUS_OUT8(address, value) \
    g1_test_out8((uintptr_t)(address), (uint8_t)(value))
#define G1_BUS_OUT32(address, value) \
    g1_test_out32((uintptr_t)(address), (uint32_t)(value))

#endif /* G1_BUS_TEST_SHIM_H */
