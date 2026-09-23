#ifndef DIRECT_RAW_PIO_MMIO_SHIM_H
#define DIRECT_RAW_PIO_MMIO_SHIM_H
#include <stdint.h>
uint8_t raw_probe_in8(uintptr_t address);
uint16_t raw_probe_in16(uintptr_t address);
uint32_t raw_probe_in32(uintptr_t address);
void raw_probe_out8(uintptr_t address, uint8_t value);
void raw_probe_out16(uintptr_t address, uint16_t value);
void raw_probe_out32(uintptr_t address, uint32_t value);
#define G1_IN8(a) raw_probe_in8(a)
#define G1_IN16(a) raw_probe_in16(a)
#define G1_IN32(a) raw_probe_in32(a)
#define G1_OUT8(a, v) raw_probe_out8(a, v)
#define G1_OUT16(a, v) raw_probe_out16(a, v)
#define G1_OUT32(a, v) raw_probe_out32(a, v)
#endif
