#include "aica_sequence_resource.h"
static void *map(void *context,uint32_t address,uint32_t bytes,int write) {
    (void)context;(void)bytes;(void)write;
    /* The wire checks the complete span inside sound sample RAM first. */
    return (void *)(uintptr_t)address;
}
void asq_resource_runtime_init(asq_resource_runtime_t *s,volatile asq_receipt_t *r) {
    asq_catalog_init(&s->catalog);
    asq_wire_init(&s->wire,&s->catalog,r,map,NULL);
}
