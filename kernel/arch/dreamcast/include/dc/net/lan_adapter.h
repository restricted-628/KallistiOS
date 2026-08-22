/* KallistiOS ##version##

   dc/net/lan_adapter.h
   Copyright (C) 2002 Megan Potter
   Copyright (C) 2026 Joseph Black

*/

/** \file    dc/net/lan_adapter.h
    \brief   LAN Adapter support.
    \ingroup lan_adapter

    This file contains declarations related to support for the HIT-0300 "LAN
    Adapter". There's not really anything that users will generally have to deal
    with in here.

    \author Megan Potter
*/

#ifndef __DC_NET_LAN_ADAPTER_H
#define __DC_NET_LAN_ADAPTER_H

#include <kos/cdefs.h>
__BEGIN_DECLS

/** \defgroup lan_adapter  LAN Adapter
    \brief                 Driver for the Dreamcast's LAN Adapter
    \ingroup               networking_drivers
*/

/* \cond */
/* Probe by resetting the inactive 8-bit expansion interface. */
int la_probe(void);

/* Return nonzero when the adapter driver is initialized. */
int la_is_initialized(void);

/* Initialize */
int la_init(void);

/* Shutdown */
int la_shutdown(void);
/* \endcond */

__END_DECLS

#endif  /* __DC_NET_LAN_ADAPTER_H */
