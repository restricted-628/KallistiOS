/* KallistiOS ##version##

   basename.c
   Copyright (C) 2020 Lawrence Sebald

*/

#include <libgen.h>

#ifdef __BASENAME_UNIT_TEST
#ifdef basename
#undef basename
#endif

#define basename bn
#endif

char *basename(char *path) {
    char *last = path, *i = path;
    char ch;

    /* If the path is empty or NULL, return a pointer to "." */
    if(!path || !*path)
        return ".";

    /* If we have a single-character path, just return it. It's probably just
       a string of "/", and this simplifies the logic below. */
    if(!path[1])
        return path;

    /* Find the end of the string, keeping track of wherever we find a '/'. */
    while((ch = *i)) {
        /* Is this a '/' character? */
        if(ch == '/') {
            /* Look ahead. Are we at the end? */
            if(!*(i + 1)) {
                /* Break out and overwrite any trailing slashes down below. */
                break;
            }

            if(*(i + 1) != '/')
                last = i + 1;
        }

        ++i;
    }

    /* Get rid of any trailing '/' characters. */
    while(i != path && *i == '/') {
        *i-- = 0;
    }

    return last;
}

#ifdef __BASENAME_UNIT_TEST
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char buf[64], buf2[64];

    /* Throw the test vectors from the POSIX spec at the function, with a few
       extras. */
    printf("basename(NULL): '%s'\n", bn(NULL));

    strcpy(buf, "usr");
    strcpy(buf2, "usr");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "usr/");
    strcpy(buf2, "usr/");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "usr//");
    strcpy(buf2, "usr//");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "usr/lib");
    strcpy(buf2, "usr/lib");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "");
    strcpy(buf2, "");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "/");
    strcpy(buf2, "/");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "//");
    strcpy(buf2, "//");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "///");
    strcpy(buf2, "///");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "/usr/");
    strcpy(buf2, "/usr/");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "/usr/lib");
    strcpy(buf2, "/usr/lib");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "/usr/lib/");
    strcpy(buf2, "/usr/lib/");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "//usr//lib//");
    strcpy(buf2, "//usr//lib//");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "//usr//lib///");
    strcpy(buf2, "//usr//lib///");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "///usr///lib///");
    strcpy(buf2, "///usr///lib///");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    strcpy(buf, "//home//dwc//test");
    strcpy(buf2, "//home//dwc//test");
    printf("basename('%s'): '%s'\n", buf, bn(buf2));

    return 0;
}
#endif
