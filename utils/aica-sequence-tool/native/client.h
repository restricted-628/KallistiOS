/* Private serialized SH4 receipt client. Frees no allocations. */
#ifndef KOS_PRIVATE_ASQ_CLIENT_H
#define KOS_PRIVATE_ASQ_CLIENT_H
#include "wire.h"
#define ASQC_SYNC 1u
#define ASQC_IDLE 2u
#define ASQC_WAIT 3u
#define ASQC_DONE 4u
#define ASQC_LOST 5u
typedef struct {
    int (*read)(void *,uint32_t *words,unsigned count);
    int (*session)(void *,uint32_t *session);
    /*1 admitted,0 definitely not admitted THIS attempt,negative uncertain.
      Backend copies the complete packet before returning. */
    int (*submit)(void *,const uint32_t packet[16]);
    void *context;
} asq_client_ops_t;
typedef struct {
    asq_client_ops_t ops;
    asq_receipt_t receipt;
    uint32_t session,cookie,packet[16],superseded[16],state;
} asq_client_t;
/* First use only, exclusive fresh owner; SYNC requires pristine receipt. */
int asq_client_init(asq_client_t *,uint32_t,const asq_client_ops_t *);
int asq_client_begin(asq_client_t *,unsigned op,unsigned slot,uint32_t generation,const uint32_t args[4]);
int asq_client_retry(asq_client_t *);
/* Cancel only a PREPARE whose ADMITTED receipt has been observed. Uncertain
   admission must resolve by exact retry/poll first; intent can remain latched
   in the outer owner. A repeated DROP retry preserves its cookie. */
int asq_client_cancel(asq_client_t *);
/* Three reads at most.1 synchronized/terminal,0 pending/I/O retry,-1 LOST/error.
   Session change, malformed coherent receipt or exhaustion never free state. */
int asq_client_poll(asq_client_t *);
int asq_client_consume(asq_client_t *);
#endif
