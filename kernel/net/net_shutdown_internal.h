/* Private checked network retirement; not a public netif ABI. */
#ifndef __NET_SHUTDOWN_INTERNAL_H
#define __NET_SHUTDOWN_INTERNAL_H

#include <stdbool.h>
#include <kos/cdefs.h>

enum net_shutdown_stage {
    NET_SHUTDOWN_PLATFORM = 1,
    NET_SHUTDOWN_NETWORK,
    NET_SHUTDOWN_ETHERNET,
    NET_SHUTDOWN_HARDWARE
};

/* Optional custom init-flag pointer bindings. The named legacy pointer must
   match exactly; check returns 0 only after that provider is safe to tear down.
   Stock manually initialized providers are still checked independently. */
struct net_shutdown_binding {
    void (*shutdown)(void);
    int (*check)(void);
};
extern const struct net_shutdown_binding arch_net_shutdown_custom;
extern const struct net_shutdown_binding arch_eth_shutdown_custom;

bool arch_net_shutdown_can_wait(void);
int arch_net_shutdown_preflight(void);
void arch_net_shutdown_wait(int error, enum net_shutdown_stage stage) __noreturn;
extern volatile int arch_net_shutdown_error;
extern volatile enum net_shutdown_stage arch_net_shutdown_stage;

int net_quiesce_checked(void);
int net_shutdown_checked(void);
bool net_shutdown_admission_closed(void);
int bba_shutdown_context_check(void);
int bba_shutdown_preflight(void);

#endif
