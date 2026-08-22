/* KallistiOS ##version##

   hardware/scif_config_internal.h
   Copyright (C) 2026 Joseph Black
*/

#ifndef __SCIF_CONFIG_INTERNAL_H
#define __SCIF_CONFIG_INTERNAL_H

#include <dc/scif.h>

typedef struct scif_register_config {
    uint16_t mode;
    uint16_t fifo;
    uint16_t control_clock;
    uint8_t bit_rate;
    uint32_t actual_baud;
    int32_t baud_error_ppm;
} scif_register_config_t;

int scif_config_encode(const scif_config_t *config,
                       scif_register_config_t *registers);

int _scif_spi_claim(void);
int _scif_spi_release(void);

#endif /* __SCIF_CONFIG_INTERNAL_H */
