#ifndef FLASHROM_LAYOUT_TEST_NET_H
#define FLASHROM_LAYOUT_TEST_NET_H

#include <stddef.h>
#include <stdint.h>

uint16_t net_crc16ccitt(const uint8_t *data, size_t length, uint16_t crc);

#endif
