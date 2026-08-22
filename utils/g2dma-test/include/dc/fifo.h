#ifndef __DC_FIFO_H
#define __DC_FIFO_H

#include <stdint.h>

extern uint32_t g2_test_fifo_status;

#define FIFO_STATUS g2_test_fifo_status
#define FIFO_SH4 UINT32_C(1)
#define FIFO_G2  UINT32_C(2)

#endif
