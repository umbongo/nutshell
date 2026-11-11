/* src/core/sse_line.h -- line assembly for a server-sent-event stream. */
#ifndef NUTSHELL_SSE_LINE_H
#define NUTSHELL_SSE_LINE_H

#include <stddef.h>

/*
 * The stream thread receives the reply as arbitrary byte runs; a "data: " line
 * can be as long as a Gemini function call with its thought signature (tens of
 * KB), so a fixed buffer cut such a line mid-JSON and the call vanished. An
 * SseLine grows on the heap up to SSE_LINE_MAX bytes per line. A longer line,
 * or one that could not be grown, is discarded whole: it is never handed to
 * the callback cut. The owner counts discarded lines (sse_line_skipped) so it
 * can say so once. Portable, no windows.h; it is the stream thread's own
 * state, never shared.
 */

/* The longest line delivered, in bytes, its terminator excluded. */
#define SSE_LINE_MAX (256 * 1024)

typedef struct {
    char  *buf;        /* heap, NUL-terminated at delivery; NULL until first use */
    size_t len;
    size_t cap;
    int    discarding; /* 1 while the rest of an oversized line is thrown away */
    int    skipped;    /* lines discarded so far */
} SseLine;

/* Called for each complete line (a trailing "\r" stripped, the line
 * NUL-terminated at line[len]). A nonzero return stops the feed. */
typedef int (*SseLineFn)(const char *line, size_t len, void *ctx);

void sse_line_init(SseLine *s);
void sse_line_free(SseLine *s);

/* Feed data[0..len). Returns 0, or the first nonzero value fn returned (the
 * rest of data is then not read). Lines may be split across calls; "\n" and
 * "\r\n" both end one. A line longer than SSE_LINE_MAX, or one whose buffer
 * cannot grow, is discarded up to its end and counted. */
int sse_line_feed(SseLine *s, const char *data, size_t len,
                  SseLineFn fn, void *ctx);

/* Lines discarded so far (0 for NULL). */
int sse_line_skipped(const SseLine *s);

/* Test hook: the allocator used to grow the buffer (realloc's contract);
 * NULL restores realloc. */
void sse_line_set_realloc_for_test(void *(*fn)(void *, size_t));

#endif
