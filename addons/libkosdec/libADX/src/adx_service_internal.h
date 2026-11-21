#ifndef MEDIA_ADX_SERVICE_INTERNAL_H
#define MEDIA_ADX_SERVICE_INTERNAL_H

#include <kosdec/adx_service.h>

/* Joint capacity is at most INT32_MAX bytes. Publish format readiness
   and the available DATA count together at the producer boundary. */
#define MEDIA_ADX_INPUT_FORMAT_READY UINT32_C(0x80000000)

#endif
