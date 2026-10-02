#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
typedef int64_t _off64_t;
#define O_MODE_MASK O_ACCMODE
#define O_DIR 0x100000
#define IOCTL_FS_ROOTBUS_DMA_READY 0x8001
#ifndef __is_aligned
#define __is_aligned(p, a) (((uintptr_t)(p) & ((a)-1)) == 0)
#endif
