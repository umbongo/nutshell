#ifndef CONGA_CORE_XMALLOC_H
#define CONGA_CORE_XMALLOC_H

#include <stddef.h>

void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);

#endif