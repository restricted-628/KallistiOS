#ifndef __DC_VECTOR_H
#define __DC_VECTOR_H

typedef __attribute__((aligned(8))) float matrix_t[4][4];

typedef struct vectorstr {
    float x;
    float y;
    float z;
    float w;
} vector_t;

typedef vector_t point_t;

#endif /* __DC_VECTOR_H */
