/* src/core/sse_line.c -- see sse_line.h. */
#include "sse_line.h"

#include <stdlib.h>
#include <string.h>

#define SSE_LINE_FIRST_CAP 4096

static void *(*g_realloc)(void *, size_t) = realloc;

void sse_line_set_realloc_for_test(void *(*fn)(void *, size_t))
{
    g_realloc = fn ? fn : realloc;
}

void sse_line_init(SseLine *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

void sse_line_free(SseLine *s)
{
    if (!s) return;
    free(s->buf);
    memset(s, 0, sizeof(*s));
}

int sse_line_skipped(const SseLine *s)
{
    return s ? s->skipped : 0;
}

/* Throw the line away: the rest of it is read and dropped up to its "\n". */
static void discard(SseLine *s)
{
    s->discarding = 1;
    s->len = 0;
}

/* Room for one more byte plus the terminator; 0 when it cannot be had. */
static int grow(SseLine *s)
{
    if (s->len + 2 <= s->cap) return 1;
    size_t want = s->cap ? s->cap * 2 : SSE_LINE_FIRST_CAP;
    /* a CRLF line of exactly SSE_LINE_MAX bytes holds one more before the
     * "\r" is stripped, and the terminator needs a slot */
    if (want > (size_t)SSE_LINE_MAX + 2) want = (size_t)SSE_LINE_MAX + 2;
    if (want < s->len + 2) return 0;
    char *nb = (char *)g_realloc(s->buf, want);
    if (!nb) return 0;
    s->buf = nb;
    s->cap = want;
    return 1;
}

int sse_line_feed(SseLine *s, const char *data, size_t len,
                  SseLineFn fn, void *ctx)
{
    if (!s || !data) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            if (s->discarding) {
                s->discarding = 0;
                s->skipped++;
                s->len = 0;
                continue;
            }
            if (s->len > 0 && s->buf[s->len - 1] == '\r') s->len--;
            if (s->len > (size_t)SSE_LINE_MAX) {   /* only a line the "\r" made long */
                s->skipped++;
                s->len = 0;
                continue;
            }
            size_t n = s->len;
            s->len = 0;
            if (n == 0) { if (fn) { int rc = fn("", 0, ctx); if (rc) return rc; } continue; }
            s->buf[n] = '\0';
            if (fn) {
                int rc = fn(s->buf, n, ctx);
                if (rc) return rc;
            }
            continue;
        }
        if (s->discarding) continue;
        /* the bound is on the line's bytes; one more is held for a "\r" */
        if (s->len >= (size_t)SSE_LINE_MAX + 1 || !grow(s)) {
            discard(s);
            continue;
        }
        s->buf[s->len++] = c;
    }
    return 0;
}
