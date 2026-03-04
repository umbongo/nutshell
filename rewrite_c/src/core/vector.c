#include "vector.h"
#include "xmalloc.h"
#include <stdlib.h>

#define VEC_INIT_CAP ((size_t)8)

void vec_init(Vector *v)
{
    v->data     = NULL;
    v->size     = 0;
    v->capacity = 0;
}

void vec_push(Vector *v, void *item)
{
    if (v->size >= v->capacity) {
        size_t new_cap = (v->capacity == 0) ? VEC_INIT_CAP : v->capacity * 2u;
        v->data     = xrealloc(v->data, new_cap * sizeof(void *));
        v->capacity = new_cap;
    }
    v->data[v->size] = item;
    v->size++;
}

void *vec_get(const Vector *v, size_t index)
{
    if (index >= v->size) {
        return NULL;
    }
    return v->data[index];
}

void vec_set(Vector *v, size_t index, void *item)
{
    if (index < v->size) {
        v->data[index] = item;
    }
}

void vec_remove(Vector *v, size_t index)
{
    if (index >= v->size) {
        return;
    }
    for (size_t i = index; i + 1u < v->size; i++) {
        v->data[i] = v->data[i + 1u];
    }
    v->size--;
}

size_t vec_size(const Vector *v)
{
    return v->size;
}

void vec_free(Vector *v)
{
    free(v->data);
    v->data     = NULL;
    v->size     = 0;
    v->capacity = 0;
}
