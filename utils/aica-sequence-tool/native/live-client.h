#ifndef KOS_PRIVATE_ASQL_CLIENT_H
#define KOS_PRIVATE_ASQL_CLIENT_H
#include "live.h"
#include "client.h"
typedef struct {
    asq_client_ops_t ops;
    asql_receipt_t receipt;
    uint32_t session,cookie,packet[16],superseded[16],state;
} asql_client_t;
/* Fresh exclusive owner only. Same SYNC/IDLE/WAIT/DONE/LOST states and three-read
   transport convention as ASQ publication. No operation frees any storage. */
int asql_client_init(asql_client_t *,uint32_t,const asq_client_ops_t *);
int asql_client_begin(asql_client_t *,unsigned op,unsigned port,uint32_t generation,const uint32_t args[4]);
int asql_client_poll(asql_client_t *);
int asql_client_retry(asql_client_t *);
/* Only an observed pending CONTINUE may be superseded; use same-port STOP or
   STOP_ALL. Preserve exact identity until its result, including after timeout. */
int asql_client_cancel(asql_client_t *,int all);
int asql_client_consume(asql_client_t *);
#endif
