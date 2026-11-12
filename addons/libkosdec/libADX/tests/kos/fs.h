/* Host-only KOS VFS signature fixture, not a filesystem implementation. */
#ifndef ADX_TEST_KOS_FS_H
#define ADX_TEST_KOS_FS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <stdio.h>
typedef int file_t;
typedef int64_t _off64_t;
_off64_t fs_seek64(file_t, _off64_t, int);
ssize_t fs_read(file_t, void *, size_t);
int64_t fs_total64(file_t);
#endif
