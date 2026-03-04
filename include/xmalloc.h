#ifndef XMALLOC_H
#define XMALLOC_H

/*
 * Safe memory allocation wrappers.
 *
 * All functions abort the process on allocation failure rather than
 * returning NULL. Callers must never check the return value for NULL.
 *
 * After freeing memory, set the pointer to NULL manually to avoid
 * use-after-free bugs (ASan will catch dangling pointer accesses).
 */

#include <stddef.h>

/* Allocate size bytes. Aborts on failure. */
void *xmalloc(size_t size);

/* Resize allocation. Aborts on failure. The original pointer is
 * invalid after a successful call. */
void *xrealloc(void *ptr, size_t size);

/* Allocate nmemb*size bytes, zero-initialised. Aborts on failure. */
void *xcalloc(size_t nmemb, size_t size);

/* Duplicate a string. Returns NULL if s is NULL. Aborts on OOM. */
char *xstrdup(const char *s);

#endif /* XMALLOC_H */
