#ifndef __DC_MATRIX_H
#define __DC_MATRIX_H

#include <kos/cdefs.h>
__BEGIN_DECLS

typedef __attribute__((aligned(8))) float matrix_t[4][4];

void mat_store(matrix_t *out);
void mat_load(const matrix_t *src);

__END_DECLS
#endif /* __DC_MATRIX_H */
