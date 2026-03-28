#ifndef NUTSHELL_CORE_SECURE_ZERO_H
#define NUTSHELL_CORE_SECURE_ZERO_H

#include <stddef.h>

/* Portable secret-zeroing: volatile prevents the compiler from eliding
 * the wipe when the buffer is not read afterwards (e.g. stack passwords). */
static inline void secure_zero(void *p, size_t n)
{
    volatile unsigned char *vp = (volatile unsigned char *)p;
    while (n--) *vp++ = 0;
}

#endif
