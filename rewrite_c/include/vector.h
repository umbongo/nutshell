#ifndef VECTOR_H
#define VECTOR_H

/*
 * Dynamic array (vector) of void pointers.
 *
 * Ownership of the stored pointers is NOT managed by the vector —
 * callers are responsible for freeing the pointed-to data before
 * or after calling vec_free().
 *
 * Example:
 *   Vector v;
 *   vec_init(&v);
 *   vec_push(&v, xstrdup("hello"));
 *   char *s = vec_get(&v, 0);
 *   free(s);
 *   vec_free(&v);
 */

#include <stddef.h>

typedef struct {
    void   **data;
    size_t   size;
    size_t   capacity;
} Vector;

/* Initialise a zero-length vector. Must be called before any other op. */
void   vec_init(Vector *v);

/* Append item to the end. Grows the backing array as needed. */
void   vec_push(Vector *v, void *item);

/* Return the item at index, or NULL if index is out of bounds. */
void  *vec_get(const Vector *v, size_t index);

/* Replace the item at index. No-op if index is out of bounds. */
void   vec_set(Vector *v, size_t index, void *item);

/* Remove the item at index, shifting later elements left. No-op if
 * index is out of bounds. Does NOT free the removed item. */
void   vec_remove(Vector *v, size_t index);

/* Return the number of items in the vector. */
size_t vec_size(const Vector *v);

/* Free the backing array. Does NOT free stored item pointers.
 * The vector is left in the initialised-empty state. */
void   vec_free(Vector *v);

#endif /* VECTOR_H */
