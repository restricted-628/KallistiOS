#ifndef FLASHROM_LAYOUT_TEST_SYSCALLS_H
#define FLASHROM_LAYOUT_TEST_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>

int syscall_flashrom_info(uint32_t part, void *info);
int syscall_flashrom_read(uint32_t pos, void *dest, size_t n);
int syscall_flashrom_write(uint32_t pos, const void *src, size_t n);
int syscall_flashrom_delete(uint32_t pos);

#endif
