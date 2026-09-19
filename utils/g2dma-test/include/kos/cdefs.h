#ifndef __KOS_CDEFS_H
#define __KOS_CDEFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#define __is_aligned(value, alignment) \
    ((((uintptr_t)(value)) & ((uintptr_t)(alignment) - 1u)) == 0)

#endif
