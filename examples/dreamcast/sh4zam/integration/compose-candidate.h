/* KallistiOS ##version##
   Benchmark-only matrix composition candidate. Not a public KOS API.
   Copyright (C) 2026 Joseph Black
*/
#ifndef KOS_COMPOSE_CANDIDATE_H
#define KOS_COMPOSE_CANDIDATE_H
#include <dc/matrix.h>
int compose_candidate(matrix_t *out, const matrix_t *lhs, const matrix_t *rhs);
#endif
