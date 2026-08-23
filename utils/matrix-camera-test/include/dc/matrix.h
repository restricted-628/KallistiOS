#ifndef __DC_MATRIX_H
#define __DC_MATRIX_H

#include <kos/cdefs.h>
__BEGIN_DECLS

#include <dc/vector.h>

void mat_apply(const matrix_t *src);
int mat_compose(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs);

__END_DECLS
#endif /* __DC_MATRIX_H */
