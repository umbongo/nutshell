#include "xmalloc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p && size > 0) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = xmalloc(len);
    memcpy(p, s, len);
    return p;
}

void *xcalloc(size_t nmemb, size_t size) {
    void *p = calloc(nmemb, size);
    if (!p && nmemb > 0 && size > 0) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p && size > 0) {
        fputs("Out of memory\n", stderr);
        abort();
    }
    return p;
}