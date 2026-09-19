/* KallistiOS ##version##

   dirname.c
   Copyright (C) 2020 Lawrence Sebald

*/

#include <libgen.h>

#ifdef __DIRNAME_UNIT_TEST
#ifdef dirname
#undef dirname
#endif

#define dirname dn
#endif

char *dirname(char *path) {
    char *last = path, *i = path;
    char ch;

    /* If the path is empty or NULL, return a pointer to "." */
    if(!path || !*path)
        return ".";

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

    /* Handle a few edge cases, like a string of nothing but slashes and
       children of the current directory... */
    if(last == path) {
        if(path[1] == '/')
            return "/";
        return ".";
    }
    /* ... or a direct subdirectory of /. */
    else if(last == path + 1) {
        return "/";
    }

    /* Get rid of any trailing '/' characters on the dirname. */
    i = last - 1;

    while(i != path && *i == '/') {
        *i-- = 0;
    }

    return path;
}

#ifdef __DIRNAME_UNIT_TEST
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char buf[64], buf2[64];

    /* Throw the test vectors from the POSIX spec at the function, with a few
       extras. */
    printf("dirname(NULL): '%s'\n", dn(NULL));

    strcpy(buf, "usr");
    strcpy(buf2, "usr");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "usr/");
    strcpy(buf2, "usr/");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "usr//");
    strcpy(buf2, "usr//");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "usr/lib");
    strcpy(buf2, "usr/lib");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "");
    strcpy(buf2, "");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "/");
    strcpy(buf2, "/");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "//");
    strcpy(buf2, "//");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "///");
    strcpy(buf2, "///");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "/usr/");
    strcpy(buf2, "/usr/");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "/usr/lib");
    strcpy(buf2, "/usr/lib");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "/usr/lib/");
    strcpy(buf2, "/usr/lib/");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "//usr//lib//");
    strcpy(buf2, "//usr//lib//");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "//usr//lib///");
    strcpy(buf2, "//usr//lib///");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "///usr///lib///");
    strcpy(buf2, "///usr///lib///");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    strcpy(buf, "//home//dwc//test");
    strcpy(buf2, "//home//dwc//test");
    printf("dirname('%s'): '%s'\n", buf, dn(buf2));

    return 0;
}
#endif
