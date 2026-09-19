/* KallistiOS ##version##

   maple_globals.c
   (c)2002 Megan Potter
   Copyright (C) 2026 Joseph Black
 */

#include <dc/maple.h>
#include <stddef.h>

/* Preserve the established public light-gun prefix of maple_state_t. New
   scheduler state must remain after gun_y rather than shifting these fields. */
_Static_assert(offsetof(maple_state_t, gun_x)
               == offsetof(maple_state_t, gun_port) + sizeof(int),
               "maple_state_t gun_x ABI offset changed");
_Static_assert(offsetof(maple_state_t, gun_y)
               == offsetof(maple_state_t, gun_x) + sizeof(int),
               "maple_state_t gun_y ABI offset changed");

/* Global state info */
maple_state_t   maple_state = {
    .driver_list = LIST_HEAD_INITIALIZER(maple_driver_list)
};
