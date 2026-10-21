/* KallistiOS ##version##

   kernel/net/net_core.c

   Copyright (C) 2002 Megan Potter
   Copyright (C) 2005, 2013 Lawrence Sebald
   Copyright (C) 2026 Joseph Black
*/

#include <string.h>
#include <errno.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include "net_shutdown_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <kos/net.h>
#include <kos/fs_socket.h>
#include <kos/dbglog.h>
#include <kos/workqueue.h>

#include "net_core.h"
#include "net_dhcp.h"
#include "net_ipv4.h"
#include "net_ipv6.h"

/*

This module, and others in this tree, handle the architecture-independent
part of the networking system of KOS. They serve the following purposes:

- Specific network card drivers may register themselves using the functions
  here, if their hardware is present
- Link-level messages will be handled here, such as ARP
- The whole networking system can be enabled or disabled from here

*/

/**************************************************************************/
/* Variables */

/* Active network devices list */
struct netif_list net_if_list = LIST_HEAD_INITIALIZER(0);

/* Are we already initialized? */
static int net_initted = 0;
static bool net_closing;
static bool net_quiesced;
static bool net_shutdown_busy;
extern int bba_shutdown_context_check(void) __weak_symbol;
extern int bba_shutdown_preflight(void) __weak_symbol;

bool net_shutdown_admission_closed(void) {
    return net_closing;
}

/* Default net device */
netif_t *net_default_dev = NULL;

workqueue_t *net_wq;

/**************************************************************************/
/* Driver list management
   Note that this stuff might be used before net_core is actually
   itself initialized. This lets us let the drivers build up the available
   driver list, and then we'll come back later and look through them all. */

/* Register a network device */
int net_reg_device(netif_t *device) {
    irq_disable_scoped();
    if(net_closing) {
        errno = EBUSY;
        return -1;
    }

    /* Make sure it's not already registered */
    if(device->flags & NETIF_REGISTERED) {
        dbglog(DBG_WARNING, "net_reg_device: '%s' is already registered\n",
               device->name);
        return -1;
    }

    /* Insert it into the interface list */
    LIST_INSERT_HEAD(&net_if_list, device, if_list);

    /* Mark it as registered */
    device->flags |= NETIF_REGISTERED;

    /* We need to do more processing in here eventually like looking for
       duplicate device IDs and assigning new indices, but that can
       wait until we're actually supporting a box with the possibility
       of more than one network interface... which probably won't happen
       any time soon... */

    /* Success */
    return 0;
}

/* Unregister a network device */
int net_unreg_device(netif_t *device) {
    irq_disable_scoped();
    if(net_closing) {
        errno = EBUSY;
        return -1;
    }

    /* Make sure it really was registered */
    if(!(device->flags & NETIF_REGISTERED)) {
        dbglog(DBG_WARNING, "net_unreg_device: '%s' isn't registered\n",
               device->name);
        return -1;
    }

    /* Remove it from the list */
    LIST_REMOVE(device, if_list);

    /* Mark it as unregistered */
    device->flags &= ~NETIF_REGISTERED;

    /* Success */
    return 0;
}

struct netif_list * net_get_if_list(void) {
    return &net_if_list;
}

/*****************************************************************************/
/* Init/shutdown */

/* Set default */
netif_t *net_set_default(netif_t *n) {
    netif_t *olddev = net_default_dev;

    net_default_dev = n;

    return olddev;
}

/* Device detect / init */
int net_dev_init(void) {
    int detected = 0;
    netif_t *cur;

    LIST_FOREACH(cur, &net_if_list, if_list) {
        /* Make sure we have one */
        if(cur->if_detect(cur) < 0)
            continue;

        /* Ok, we do -- initialize it */
        if(cur->if_init(cur) < 0)
            continue;

        /* It's initialized, so now enable it */
        if(cur->if_start(cur) < 0) {
            cur->if_shutdown(cur);
            continue;
        }

        /* Set the first detected device to be the default */
        if(net_default_dev == NULL)
            net_set_default(cur);

        detected++;
    }

    dbglog(DBG_DEBUG, "net_dev_init: detected %d usable network device(s)\n", detected);

    if(detected)
        return 0;
    else
        return -1;
}

/* Init */
int net_init(uint32_t ip) {
    int rv = 0;

    irq_mask_t sr = irq_disable();
    if(net_closing || net_shutdown_busy) {
        irq_restore(sr);
        errno = EBUSY;
        return -1;
    }
    if(net_initted) {
        irq_restore(sr);
        return 0;
    }
    net_shutdown_busy = true;
    irq_restore(sr);

    /* Detect and potentially initialize devices. Retain partial driver state. */
    if(net_dev_init() < 0)
        goto fail;

    net_wq = workqueue_create();
    if(!net_wq)
        goto fail;

    /* Initialize the ARP cache */
    net_arp_init();

    /* Initialize the NDP cache */
    net_ndp_init();

    /* Initialize IPv4 fragmentation support */
    net_ipv4_frag_init();

    /* Initialize multicast support */
    net_multicast_init();

    /* Initialize IPv6 support */
    net_ipv6_init();

    /* Initialize the sockets-like interface */
    fs_socket_init();

    /* Initialize the UDP/TCP systems */
    net_udp_init();
    net_tcp_init();

    /* Initialize the DHCP system */
    net_dhcp_init();

    if(net_default_dev) {
        /* Did we get a requested IP address? this normally happens over dcload-ip. */
        if(ip) {
            rv = net_dhcp_request(ip);
            if(rv < 0) {
                dbglog(DBG_DEBUG, "Failed to acquire the specified IP with DHCP\n");

                /* If that fails, set the address manually. Although gateway
                   and dns etc. will also manually need setting */
                net_ipv4_parse_address(ip, net_default_dev->ip_addr);
                rv = 0;
            }
        }

        /* We didn't get a requested IP address, if we don't already have one
           set, then do so via DHCP. */
        else if(!net_default_dev->ip_addr[0])
            rv = net_dhcp_request(0);
    }

    net_initted = 1;
    net_shutdown_busy = false;
    return rv;
fail:
    net_shutdown_busy = false;
    return -1;
}

/* Quiescence is retryable and retains protocol consumers and hardware. */
int net_quiesce_checked(void) {
    netif_t *cur;
    irq_mask_t sr;

    if(!arch_net_shutdown_can_wait()) {
        errno = EPERM;
        return -1;
    }
    sr = irq_disable();
    if(net_shutdown_busy) {
        irq_restore(sr);
        errno = EBUSY;
        return -1;
    }
    net_shutdown_busy = true;
    irq_restore(sr);
    /* get_thread takes a mutex: lifecycle ownership, not IRQ masking, pins it. */
    if(net_wq && workqueue_get_thread(net_wq) == thd_get_current()) {
        net_shutdown_busy = false;
        errno = EDEADLK;
        return -1;
    }
    if(bba_shutdown_context_check && bba_shutdown_context_check() < 0) {
        net_shutdown_busy = false;
        return -1;
    }
    net_closing = true;

    if(!net_quiesced) {
        /* First close TX/RX so a workqueue job blocked in TX can finish. */
        LIST_FOREACH(cur, &net_if_list, if_list) {
            if(cur->flags & NETIF_RUNNING && cur->if_stop
                    && cur->if_stop(cur) < 0)
                goto refuse;
        }
        if(net_wq) {
            workqueue_kill(net_wq);
            if(workqueue_get_thread(net_wq) != NULL) {
                errno = EBUSY;
                goto refuse;
            }
        }
        net_quiesced = true;
    }
    net_shutdown_busy = false;
    return 0;

refuse:
    net_shutdown_busy = false;
    return -1;
}

int net_shutdown_checked(void) {
    netif_t *cur;
    if(net_quiesce_checked() < 0)
        return -1;
    /* Software stop does not grant permission to release retained hardware. */
    if(bba_shutdown_preflight && bba_shutdown_preflight() < 0)
        return -1;
    irq_mask_t sr = irq_disable();
    if(net_shutdown_busy) {
        irq_restore(sr);
        errno = EBUSY;
        return -1;
    }
    net_shutdown_busy = true;
    irq_restore(sr);

    /* Check every device before destroying any protocol consumer. */
    LIST_FOREACH(cur, &net_if_list, if_list) {
        if(cur->flags & NETIF_INITIALIZED && cur->if_shutdown
                && cur->if_shutdown(cur) < 0) {
            net_shutdown_busy = false;
            return -1;
        }
    }
    if(!net_initted) {
        net_shutdown_busy = false;
        return 0;
    }

    /* Shut down DHCP */
    net_dhcp_shutdown();

    /* Shut down the UDP/TCP systems */
    net_tcp_shutdown();
    net_udp_shutdown();

    /* Shut down the sockets-like interface */
    fs_socket_shutdown();

    /* Shut down IPv6 support */
    net_ipv6_shutdown();

    /* Shut down multicast support */
    net_multicast_shutdown();

    /* Shut down IPv4 fragmentation support */
    net_ipv4_frag_shutdown();

    /* Shut down the NDP cache */
    net_ndp_shutdown();

    /* Shut down the ARP cache */
    net_arp_shutdown();

    /* Shut down the network thread */
    workqueue_destroy(net_wq);

    net_set_default(NULL);
    net_initted = 0;
    net_wq = NULL;
    net_shutdown_busy = false;
    return 0;
}

void net_shutdown(void) {
    if(net_shutdown_checked() < 0)
        arch_net_shutdown_wait(errno, NET_SHUTDOWN_NETWORK);
}
