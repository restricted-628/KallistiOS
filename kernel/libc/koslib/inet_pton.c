/* KallistiOS ##version##

   inet_pton.c
   Copyright (C) 2007, 2010 Lawrence Sebald

*/

#include <arpa/inet.h>
#include <errno.h>

static int inet_pton4(const char *src, void *dst) {
    int parts[4] = { 0 };
    int count = 0;
    struct in_addr *addr = (struct in_addr *)dst;

    for(; *src && count < 4; ++src) {
        if(*src == '.') {
            ++count;
        }
        /* Unlike inet_aton(), inet_pton() only supports decimal parts */
        else if(*src >= '0' && *src <= '9') {
            parts[count] *= 10;
            parts[count] += *src - '0';
        }
        else {
            /* Invalid digit, and not a dot... bail */
            return 0;
        }
    }

    if(count != 3) {
        /* Not the right number of parts, bail */
        return 0;
    }

    /* Validate each part, note that unlike inet_aton(), inet_pton() only
       supports the standard xxx.xxx.xxx.xxx addresses. */
    if(parts[0] > 0xFF || parts[1] > 0xFF ||
            parts[2] > 0xFF || parts[3] > 0xFF)
        return 0;

    addr->s_addr = htonl(parts[0] << 24 | parts[1] << 16 |
                         parts[2] << 8 | parts[3]);

    return 1;
}

static int inet_pton6(const char *src, void *dst) {
    uint32_t parts[8] = { 0 };
    struct in6_addr *addr = (struct in6_addr *)dst;
    int wc = 0, dc = 0, afterdc = 0;
    int pos = 0, i;
    const char *tmp = src, *ip4start = NULL;
    struct in_addr ip4addr;

    /* This loop simply checks the address for validity. Its split up in two
       parts (check and parse) like this for simplicity and clarity. */
    for(; *tmp; ++tmp) {
        if(*tmp == ':') {
            if(wc && dc) {
                /* If we have a double colon again after a double colon (or we
                   have 3 colons in a row), its an error. Bail out. */
                return 0;
            }
            else if(ip4start) {
                /* If we have any dots, we can't have any colons after! */
                return 0;
            }
            else if(wc) {
                dc = 1;
            }
            else if(dc) {
                ++afterdc;
            }

            wc = 1;
        }
        else if(*tmp == '.') {
            /* If this is the first part of the IPv4, figure out where it starts
               in the string */
            if(!ip4start) {
                for(ip4start = tmp; ip4start > src; --ip4start) {
                    if(*ip4start == ':') {
                        ++ip4start;
                        break;
                    }
                }
            }
        }
        else if((*tmp >= '0' && *tmp <= '9') || (*tmp >= 'A' && *tmp <= 'F') ||
                (*tmp >= 'a' && *tmp <= 'f')) {
            wc = 0;
        }
        else {
            /* Invalid character encountered, bail out */
            return 0;
        }
    }

    /* Make sure if we have a colon at the end, its a double colon, not a single
       colon. Double colon is fine, single colon is invalid. */
    if(*(tmp - 1) == ':' && *(tmp - 2) != ':') {
        return 0;
    }

    /* Same deal at the beginning of the string. */
    if(*src == ':' && *(src + 1) != ':') {
        return 0;
    }

    /* If we have any dots, attempt to parse out the IPv4 address. */
    if(ip4start && inet_pton4(ip4start, &ip4addr) != 1) {
        return 0;
    }

    /* Adjust the after double colon count for embedded IPv4 addresses. */
    if(ip4start && dc) {
        afterdc += 2;
    }

    ++afterdc;

    /* Reset these, since we need them reset below to start the parsing. */
    wc = dc = 0;

    for(; *src && (!ip4start || src < ip4start); ++src) {
        if(*src == ':') {
            if(wc) {
                /* We have a double colon, advance as far as we need to. */
                if(pos + afterdc >= 8) {
                    /* The double colon is invalid wherever it is. */
                    return 0;
                }

                dc = 1;
                pos = 8 - afterdc;
            }
            else {
                /* Advance to the next 16-bit set */
                wc = 1;
                ++pos;
            }
        }
        else if(*src >= '0' && *src <= '9') {
            parts[pos] <<= 4;
            parts[pos] |= *src - '0';
            wc = 0;
        }
        else if(*src >= 'a' && *src <= 'f') {
            parts[pos] <<= 4;
            parts[pos] |= *src - 'a' + 0x0A;
            wc = 0;
        }
        else if(*src >= 'A' && *src <= 'F') {
            parts[pos] <<= 4;
            parts[pos] |= *src - 'A' + 0x0A;
            wc = 0;
        }

        if(parts[pos] > 0xFFFF) {
            /* We've overflowed, bail */
            return 0;
        }
    }

    if((!ip4start && pos != 7) || (ip4start && pos != 5)) {
        /* We didn't fill in the whole address... */
        return 0;
    }

    /* If we've gotten here, everything's good, so fill in the real address. */
    for(i = 0; i < 8; ++i) {
        addr->__s6_addr.__s6_addr16[i] = htons((uint16_t)parts[i]);
    }

    /* If we have an IPv4 address embedded, put it in too. */
    if(ip4start) {
        addr->__s6_addr.__s6_addr32[3] = ip4addr.s_addr;
    }

    /* And, we're done. */
    return 1;
}

int inet_pton(int af, const char *src, void *dst) {
    switch(af) {
        case AF_INET:
            return inet_pton4(src, dst);

        case AF_INET6:
            return inet_pton6(src, dst);

        default:
            errno = EAFNOSUPPORT;
            return -1;
    }
}
