#include "test_framework.h"
#include "sse_line.h"
#include <stdlib.h>
#include <string.h>

/* Collects the lines the callback sees, joined with '|'. */
typedef struct {
    char  *text;
    size_t len;
    int    count;
    int    stop_after;   /* callback returns 7 at this count, 0 = never */
} Got;

static int got_line(const char *line, size_t len, void *ctx)
{
    Got *g = (Got *)ctx;
    char *nt = (char *)realloc(g->text, g->len + len + 2);
    if (!nt) return 1;
    g->text = nt;
    memcpy(g->text + g->len, line, len);
    g->len += len;
    g->text[g->len++] = '|';
    g->text[g->len] = '\0';
    g->count++;
    return g->stop_after && g->count == g->stop_after ? 7 : 0;
}

static void got_free(Got *g) { free(g->text); memset(g, 0, sizeof(*g)); }

int test_sse_line_split_across_feeds(void)
{
    TEST_BEGIN();
    SseLine s;
    Got g;
    memset(&g, 0, sizeof(g));
    sse_line_init(&s);
    ASSERT_EQ(sse_line_feed(&s, "data: ab", 8, got_line, &g), 0);
    ASSERT_EQ(g.count, 0);
    ASSERT_EQ(sse_line_feed(&s, "cd\ndata: e", 10, got_line, &g), 0);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(sse_line_feed(&s, "f\n\ndata: g\n", 11, got_line, &g), 0);
    ASSERT_STR_EQ(g.text, "data: abcd|data: ef||data: g|");
    ASSERT_EQ(sse_line_skipped(&s), 0);
    /* a byte at a time */
    got_free(&g);
    const char *in = "xy\nzw\n";
    for (int i = 0; i < 6; i++) sse_line_feed(&s, in + i, 1, got_line, &g);
    ASSERT_STR_EQ(g.text, "xy|zw|");
    got_free(&g);
    sse_line_free(&s);
    ASSERT_NULL(s.buf);
    TEST_END();
}

int test_sse_line_crlf_and_lf(void)
{
    TEST_BEGIN();
    SseLine s;
    Got g;
    memset(&g, 0, sizeof(g));
    sse_line_init(&s);
    const char *in = "a\r\nb\nc\r\n\r\nd\r";
    sse_line_feed(&s, in, strlen(in), got_line, &g);
    ASSERT_STR_EQ(g.text, "a|b|c||");          /* "d\r" is still open */
    sse_line_feed(&s, "\n", 1, got_line, &g);
    ASSERT_STR_EQ(g.text, "a|b|c||d|");
    /* CR split from its LF across two feeds */
    sse_line_feed(&s, "e\r", 2, got_line, &g);
    sse_line_feed(&s, "\n", 1, got_line, &g);
    ASSERT_STR_EQ(g.text, "a|b|c||d|e|");
    got_free(&g);
    sse_line_free(&s);
    TEST_END();
}

int test_sse_line_callback_stops_the_feed(void)
{
    TEST_BEGIN();
    SseLine s;
    Got g;
    memset(&g, 0, sizeof(g));
    g.stop_after = 2;
    sse_line_init(&s);
    ASSERT_EQ(sse_line_feed(&s, "1\n2\n3\n", 6, got_line, &g), 7);
    ASSERT_STR_EQ(g.text, "1|2|");
    got_free(&g);
    sse_line_free(&s);
    TEST_END();
}

/* One line of `n` 'x' bytes plus `ending`, fed in uneven runs. */
static int feed_line(SseLine *s, Got *g, size_t n, const char *ending)
{
    char *line = (char *)malloc(n + 3);
    if (!line) return -1;
    memset(line, 'x', n);
    strcpy(line + n, ending);
    size_t total = n + strlen(ending);
    size_t pos = 0, step = 1000;
    while (pos < total) {
        size_t k = total - pos < step ? total - pos : step;
        sse_line_feed(s, line + pos, k, got_line, g);
        pos += k;
        step = step * 3 + 1;
    }
    free(line);
    return 0;
}

int test_sse_line_bound_just_under_and_over(void)
{
    TEST_BEGIN();
    SseLine s;
    Got g;
    memset(&g, 0, sizeof(g));
    sse_line_init(&s);

    /* exactly the bound: delivered whole, with LF and with CRLF */
    ASSERT_EQ(feed_line(&s, &g, 262144, "\n"), 0);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ((int)g.len, 262144 + 1);
    ASSERT_EQ(sse_line_skipped(&s), 0);
    ASSERT_EQ(feed_line(&s, &g, 262144, "\r\n"), 0);
    ASSERT_EQ(g.count, 2);
    ASSERT_EQ((int)g.len, 2 * (262144 + 1));
    ASSERT_EQ(sse_line_skipped(&s), 0);

    /* one byte over: discarded whole, with LF and with CRLF */
    ASSERT_EQ(feed_line(&s, &g, 262145, "\n"), 0);
    ASSERT_EQ(g.count, 2);
    ASSERT_EQ(sse_line_skipped(&s), 1);
    ASSERT_EQ(feed_line(&s, &g, 262145, "\r\n"), 0);
    ASSERT_EQ(g.count, 2);
    ASSERT_EQ(sse_line_skipped(&s), 2);

    /* far over, then a short line: the next line is read normally */
    ASSERT_EQ(feed_line(&s, &g, 600000, "\n"), 0);
    ASSERT_EQ(sse_line_skipped(&s), 3);
    sse_line_feed(&s, "ok\n", 3, got_line, &g);
    ASSERT_EQ(g.count, 3);
    ASSERT_EQ(strcmp(g.text + g.len - 3, "ok|"), 0);

    got_free(&g);
    sse_line_free(&s);
    TEST_END();
}

static int g_fail_after;
static void *failing_realloc(void *p, size_t n)
{
    if (g_fail_after-- <= 0) return NULL;
    return realloc(p, n);
}

int test_sse_line_alloc_failure_discards_not_cuts(void)
{
    TEST_BEGIN();
    SseLine s;
    Got g;
    memset(&g, 0, sizeof(g));
    sse_line_init(&s);

    /* the first growth (4 KiB) works, the second (8 KiB) fails mid-line */
    g_fail_after = 1;
    sse_line_set_realloc_for_test(failing_realloc);
    ASSERT_EQ(feed_line(&s, &g, 9000, "\n"), 0);
    ASSERT_EQ(g.count, 0);                 /* never delivered cut */
    ASSERT_EQ(sse_line_skipped(&s), 1);

    /* the very first allocation failing discards too */
    sse_line_free(&s);
    sse_line_init(&s);
    g_fail_after = 0;
    ASSERT_EQ(feed_line(&s, &g, 10, "\n"), 0);
    ASSERT_EQ(g.count, 0);
    ASSERT_EQ(sse_line_skipped(&s), 1);

    /* with the allocator back, the next line is read */
    sse_line_set_realloc_for_test(NULL);
    sse_line_feed(&s, "next\n", 5, got_line, &g);
    ASSERT_STR_EQ(g.text, "next|");

    got_free(&g);
    sse_line_free(&s);
    TEST_END();
}
