/* pty_ring.c -- circular byte buffer between a blocking reader thread and the
 * UI thread. See pty_ring.h for the contract; no Win32, no locking, no
 * globals -- the owner (src/ui/local_pty.c) serializes all calls under one
 * critical section.
 */

#include "pty_ring.h"

#include <stdlib.h>
#include <string.h>

int pty_ring_init(PtyRing *r, size_t cap)
{
    if (r == NULL) {
        return -1;
    }

    if (cap == 0) {
        cap = PTY_RING_CAP;
    }

    unsigned char *buf = malloc(cap);
    if (buf == NULL) {
        r->buf = NULL;
        r->cap = 0;
        r->head = 0;
        r->len = 0;
        r->eof = 0;
        r->full = 0;
        return -1;
    }

    r->buf = buf;
    r->cap = cap;
    r->head = 0;
    r->len = 0;
    r->eof = 0;
    r->full = 0;
    return 0;
}

void pty_ring_free(PtyRing *r)
{
    if (r == NULL) {
        return;
    }

    free(r->buf);
    r->buf = NULL;
    r->cap = 0;
    r->head = 0;
    r->len = 0;
    r->eof = 0;
    r->full = 0;
}

size_t pty_ring_push(PtyRing *r, const void *data, size_t len)
{
    if (r == NULL || r->buf == NULL || data == NULL || len == 0) {
        return 0;
    }

    size_t space = r->cap - r->len;
    size_t take = len;
    if (take > space) {
        take = space;
    }

    if (take < len) {
        r->full = 1;
    }

    if (take == 0) {
        return 0;
    }

    const unsigned char *src = (const unsigned char *)data;
    size_t tail = (r->head + r->len) % r->cap;
    size_t first = r->cap - tail;
    if (first > take) {
        first = take;
    }

    memcpy(r->buf + tail, src, first);
    size_t remaining = take - first;
    if (remaining > 0) {
        memcpy(r->buf, src + first, remaining);
    }

    r->len += take;
    return take;
}

size_t pty_ring_drain(PtyRing *r, void *out, size_t max)
{
    if (r == NULL || r->buf == NULL || out == NULL || max == 0) {
        return 0;
    }

    size_t take = max;
    if (take > r->len) {
        take = r->len;
    }

    if (take == 0) {
        return 0;
    }

    unsigned char *dst = (unsigned char *)out;
    size_t first = r->cap - r->head;
    if (first > take) {
        first = take;
    }

    memcpy(dst, r->buf + r->head, first);
    size_t remaining = take - first;
    if (remaining > 0) {
        memcpy(dst + first, r->buf, remaining);
    }

    r->head = (r->head + take) % r->cap;
    r->len -= take;
    return take;
}

size_t pty_ring_available(const PtyRing *r)
{
    if (r == NULL || r->buf == NULL) {
        return 0;
    }
    return r->len;
}

size_t pty_ring_space(const PtyRing *r)
{
    if (r == NULL || r->buf == NULL) {
        return 0;
    }
    return r->cap - r->len;
}

void pty_ring_set_eof(PtyRing *r)
{
    if (r == NULL) {
        return;
    }
    r->eof = 1;
}

int pty_ring_eof(const PtyRing *r)
{
    if (r == NULL) {
        return 0;
    }
    return r->eof;
}

int pty_ring_full(const PtyRing *r)
{
    if (r == NULL) {
        return 0;
    }
    return r->full;
}

void pty_ring_clear_full(PtyRing *r)
{
    if (r == NULL) {
        return;
    }
    r->full = 0;
}

void pty_ring_reset(PtyRing *r)
{
    if (r == NULL || r->buf == NULL) {
        return;
    }
    r->head = 0;
    r->len = 0;
    r->eof = 0;
    r->full = 0;
}
