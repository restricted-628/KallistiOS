#ifndef __ARCH_ARCH_H
#define __ARCH_ARCH_H

#define HW_TYPE_RETAIL 0
#define HW_MEM_16 0x01000000u
#define HW_MEM_32 0x02000000u
extern unsigned int g2_test_mem_size;
#define HW_MEMSIZE g2_test_mem_size

int hardware_sys_mode(int *region);

#endif
