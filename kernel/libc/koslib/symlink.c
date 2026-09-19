/* KallistiOS ##version##

   symlink.c
   Copyright (C) 2013 Lawrence Sebald
*/

#include <kos/fs.h>

int symlink(const char *path1, const char *path2) {
    return fs_symlink(path1, path2);
}

