#ifndef __ASIC_TEST_SHIM_H
#define __ASIC_TEST_SHIM_H

#include <stdint.h>

uint32_t asic_test_read32(uintptr_t address);
void asic_test_write32(uintptr_t address, uint32_t value);

#endif
