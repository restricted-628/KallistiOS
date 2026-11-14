#ifndef KOS_PRIVATE_ASQL_OWNER_H
#define KOS_PRIVATE_ASQL_OWNER_H
#include "live-client.h"
#include "owner.h"
#include "music_live.h"
typedef struct {
    asql_client_t client;
    aica_music_owner_t *music;
    asq_owner_t *resources;
    uint32_t session,timeout_ms;
    uint8_t initialized;
} asql_owner_t;
/* Zeroed storage, once per fresh sound runtime. Existing music/resource owners
   must be initialized, synchronized and idle in the same session. Caller keeps
   all three contexts, banks and physical pool alive, including after LOST.
   One serialized ordinary-thread owner; no other raw producers/reset/shutdown.
   This owner allocates/frees nothing and never adopts an existing control owner. */
int asql_owner_init(asql_owner_t *,aica_music_owner_t *,asq_owner_t *,uint32_t timeout_ms);
/* Other owners must be idle before command admission; do not issue mutations
   through them while this client is WAIT. PLAY/CHAIN require a currently READY
   local resource generation; ARM readiness is rechecked at control application.
   Consume DONE explicitly. A completed logical STOP never frees tone/voice RAM. */
int asql_owner_command(asql_owner_t *,unsigned op,unsigned port,uint32_t generation,const uint32_t args[4]);
int asql_owner_poll(asql_owner_t *);
int asql_owner_retry(asql_owner_t *);
int asql_owner_cancel(asql_owner_t *,int all);
int asql_owner_consume(asql_owner_t *);
#endif
