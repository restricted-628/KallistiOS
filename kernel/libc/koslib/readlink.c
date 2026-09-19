/* KallistiOS ##version##

   readlink.c
   Copyright (C) 2013 Lawrence Sebald
*/

#include <kos/fs.h>

ssize_t readlink(const char *path, char *buf, size_t bufsize) {
    return fs_readlink(path, buf, bufsize);
}
