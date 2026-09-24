#ifndef TEST_DC_VECTOR_H
#define TEST_DC_VECTOR_H

typedef __attribute__((aligned(8))) float matrix_t[4][4];

typedef struct point {
    float x;
    float y;
    float z;
    float w;
} vector_t;

typedef vector_t point_t;

#endif
