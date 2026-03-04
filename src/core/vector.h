#ifndef CONGA_CORE_VECTOR_H
#define CONGA_CORE_VECTOR_H

#include <stddef.h>

typedef struct {
    void **data;
    size_t size;
    size_t capacity;
} Vector;

void vec_init(Vector *v);
void vec_push(Vector *v, void *item);
void *vec_get(const Vector *v, size_t index);
void vec_set(Vector *v, size_t index, void *item);
void vec_remove(Vector *v, size_t index);
size_t vec_size(const Vector *v);
void vec_free(Vector *v);

#endif