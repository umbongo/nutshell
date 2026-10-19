/* tests/test_text_runs.c -- the run geometry recorded by the chat panel's
 * paint pass and the hit test over it (spec 2026-10-18 sections 3, 4, 6). */
#include "test_framework.h"
#include "text_runs.h"
#include <string.h>

/* Append the run for text[start, end) at (x, y), every UTF-16 unit 6 px
 * wide, through text_runs_cum_from_utf16 exactly as md_render.c does. */
static int add_run(TextRuns *r, const char *text, int start, int end,
                   int x, int y, int part, int flags)
{
    int dx16[64], cum[65], cb[65];
    for (int i = 0; i < 64; i++) dx16[i] = 6 * (i + 1);
    int n = text_runs_cum_from_utf16(text + start, end - start, dx16, 64,
                                     cum, cb, 65);
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = cum[n - 1]; t.h = 10;
    t.start = start; t.end = end; t.part = part; t.flags = flags;
    return text_runs_append(r, &t, cum, cb, n);
}

static int add_synth(TextRuns *r, const char *mark, int start, int end,
                     int x, int y, int w, int part)
{
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = w; t.h = 10;
    t.start = start; t.end = end; t.part = part;
    strcpy(t.synthetic, mark);
    return text_runs_append(r, &t, NULL, NULL, 0);
}

/* "Hello w\xC3\xB6rld\n- item": line A = "Hello " [0,6) at x 10 and
 * "w\xC3\xB6rld" [6,12) at x 46; line B = the bullet over its marker bytes
 * [13,15) at x 10 (16 px wide) and "item" [15,19) at x 26. */
static const char FIX[] = "Hello w\xC3\xB6rld\n- item";

static void build_fixture(TextRuns *r)
{
    text_runs_init(r);
    add_run(r, FIX, 0, 6, 10, 0, TR_PART_MAIN, 0);
    add_run(r, FIX, 6, 12, 46, 0, TR_PART_MAIN, 0);
    add_synth(r, "- ", 13, 15, 10, 14, 16, TR_PART_MAIN);
    add_run(r, FIX, 15, 19, 26, 14, TR_PART_MAIN, 0);
}

int test_text_runs_cum_ascii_multibyte_and_surrogates(void)
{
    TEST_BEGIN();
    int dx16[8] = { 6, 12, 18, 24, 30, 36, 42, 48 };
    int cum[8], cb[8];
    /* "a" + U+1F600 (4 bytes, 2 units) + "b" */
    const char *s = "a\xF0\x9F\x98\x80" "b";
    int n = text_runs_cum_from_utf16(s, 6, dx16, 4, cum, cb, 8);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(cum[0], 0);  ASSERT_EQ(cb[0], 0);
    ASSERT_EQ(cum[1], 6);  ASSERT_EQ(cb[1], 1);
    ASSERT_EQ(cum[2], 18); ASSERT_EQ(cb[2], 5);
    ASSERT_EQ(cum[3], 24); ASSERT_EQ(cb[3], 6);
    /* "\xC3\xA9" is one character, one unit */
    n = text_runs_cum_from_utf16("\xC3\xA9x", 3, dx16, 2, cum, cb, 8);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(cb[1], 2);   ASSERT_EQ(cum[1], 6);
    ASSERT_EQ(cb[2], 3);   ASSERT_EQ(cum[2], 12);
    TEST_END();
}

int test_text_runs_cum_invalid_bytes_and_limits(void)
{
    TEST_BEGIN();
    int rc;
    int dx16[4] = { 5, 10, 15, 20 };
    int cum[4], cb[4];
    /* cp1252 "\xE9" then "x": an invalid lead is one character */
    int n = text_runs_cum_from_utf16("\xE9x", 2, dx16, 2, cum, cb, 4);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(cb[1], 1);  ASSERT_EQ(cum[1], 5);
    ASSERT_EQ(cb[2], 2);  ASSERT_EQ(cum[2], 10);
    /* fewer units reported than characters: the last width repeats */
    n = text_runs_cum_from_utf16("abc", 3, dx16, 1, cum, cb, 4);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(cum[3], 5);
    /* cap too small */
    rc = text_runs_cum_from_utf16("abcd", 4, dx16, 4, cum, cb, 4);
    ASSERT_EQ(rc, -1);
    rc = text_runs_cum_from_utf16(NULL, 1, dx16, 1, cum, cb, 4);
    ASSERT_EQ(rc, -1);
    TEST_END();
}

int test_text_runs_utf8_prefix_never_splits_a_pair(void)
{
    TEST_BEGIN();
    int rc;
    const char *s = "a\xF0\x9F\x98\x80" "b";
    rc = text_runs_utf8_prefix_for_utf16(s, 6, 1);
    ASSERT_EQ(rc, 1);
    rc = text_runs_utf8_prefix_for_utf16(s, 6, 2);
    ASSERT_EQ(rc, 1);
    rc = text_runs_utf8_prefix_for_utf16(s, 6, 3);
    ASSERT_EQ(rc, 5);
    rc = text_runs_utf8_prefix_for_utf16(s, 6, 9);
    ASSERT_EQ(rc, 6);
    rc = text_runs_utf8_prefix_for_utf16(s, 6, 0);
    ASSERT_EQ(rc, 0);
    TEST_END();
}

int test_text_runs_append_assigns_lines_and_boundaries(void)
{
    TEST_BEGIN();
    TextRuns r;
    build_fixture(&r);
    ASSERT_EQ(r.n, 4);
    ASSERT_EQ(r.line_count, 2);
    ASSERT_EQ(r.v[0].line, 0);
    ASSERT_EQ(r.v[1].line, 0);
    ASSERT_EQ(r.v[2].line, 1);
    ASSERT_EQ(r.v[3].line, 1);
    ASSERT_EQ(r.v[0].n_cum, 7);          /* "Hello " */
    ASSERT_EQ(r.v[1].n_cum, 6);          /* w o-umlaut r l d */
    ASSERT_EQ(r.v[1].first_cum, 7);
    ASSERT_EQ(r.v[2].n_cum, 0);
    ASSERT_EQ(r.v[1].w, 30);
    text_runs_reset(&r);
    ASSERT_EQ(r.n, 0);
    ASSERT_EQ(r.cum_n, 0);
    ASSERT_EQ(r.line_count, 0);
    text_runs_free(&r);
    ASSERT_NULL(r.v);
    TEST_END();
}

int test_text_runs_hit_inside_a_run_takes_the_nearest_boundary(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    int part = -1;
    rc = text_runs_hit(&r, 10, 5, &part);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(part, TR_PART_MAIN);
    rc = text_runs_hit(&r, 18, 5, NULL);
    ASSERT_EQ(rc, 1);    /* 8 px in: boundary 6 */
    rc = text_runs_hit(&r, 20, 5, NULL);
    ASSERT_EQ(rc, 2);    /* 10 px in: boundary 12 */
    /* x 58 is 12 px into "w\xC3\xB6rld": boundary 2 = after the umlaut, byte 9 */
    rc = text_runs_hit(&r, 58, 5, NULL);
    ASSERT_EQ(rc, 9);
    /* never inside the two-byte character */
    rc = text_runs_hit(&r, 55, 5, NULL);
    ASSERT_TRUE(rc != 8);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_left_right_and_between(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    rc = text_runs_hit(&r, 0, 5, NULL);
    ASSERT_EQ(rc, 0);      /* left of the line */
    rc = text_runs_hit(&r, 500, 5, NULL);
    ASSERT_EQ(rc, 12);   /* right of the line */
    /* a gap between runs: move run 1 right by 20 px */
    r.v[1].x = 66;
    rc = text_runs_hit(&r, 50, 5, NULL);
    ASSERT_EQ(rc, 6);     /* end of the left run */
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_above_below_and_between_lines(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    rc = text_runs_hit(&r, 40, -30, NULL);
    ASSERT_EQ(rc, 0);   /* above everything */
    rc = text_runs_hit(&r, 40, 400, NULL);
    ASSERT_EQ(rc, 19);  /* below everything */
    /* y 12 is in the 4 px gap under line A: the line above */
    rc = text_runs_hit(&r, 500, 12, NULL);
    ASSERT_EQ(rc, 12);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_synthetic_maps_to_the_next_real_run(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    rc = text_runs_hit(&r, 12, 18, NULL);
    ASSERT_EQ(rc, 15);   /* on the bullet */
    rc = text_runs_hit(&r, 0, 18, NULL);
    ASSERT_EQ(rc, 15);    /* left of the bullet */
    /* a bullet alone on its line resolves to its own end */
    TextRuns lone;
    text_runs_init(&lone);
    add_synth(&lone, "3. ", 0, 3, 10, 0, 16, TR_PART_MAIN);
    rc = text_runs_hit(&lone, 12, 5, NULL);
    ASSERT_EQ(rc, 3);
    text_runs_free(&lone);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_reports_the_thinking_part(void)
{
    TEST_BEGIN();
    int rc;
    /* Thinking body runs above the reply text, painted first. */
    static const char THINK[] = "plan it";
    static const char MAIN[] = "done";
    TextRuns r;
    text_runs_init(&r);
    add_run(&r, THINK, 0, 5, 10, 0, TR_PART_THINKING, 0);
    add_run(&r, THINK, 5, 7, 40, 0, TR_PART_THINKING, 0);
    add_run(&r, MAIN, 0, 4, 10, 30, TR_PART_MAIN, 0);
    int part = -1;
    rc = text_runs_hit(&r, 22, 5, &part);
    ASSERT_EQ(rc, 2);
    ASSERT_EQ(part, TR_PART_THINKING);
    rc = text_runs_hit(&r, 16, 33, &part);
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(part, TR_PART_MAIN);
    /* the gap between the box and the reply belongs to the box's last line */
    rc = text_runs_hit(&r, 500, 20, &part);
    ASSERT_EQ(rc, 7);
    ASSERT_EQ(part, TR_PART_THINKING);
    /* runs on one y but different parts never form one line */
    ASSERT_EQ(r.v[2].line, 1);
    TextRuns same_y;
    text_runs_init(&same_y);
    add_run(&same_y, THINK, 0, 5, 10, 0, TR_PART_THINKING, 0);
    add_run(&same_y, MAIN, 0, 4, 60, 0, TR_PART_MAIN, 0);
    ASSERT_EQ(same_y.n, 2);
    ASSERT_EQ(same_y.v[0].line, 0);
    ASSERT_EQ(same_y.v[1].line, 1);
    ASSERT_EQ(same_y.line_count, 2);
    text_runs_free(&same_y);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_table_cells_share_a_line_by_y(void)
{
    TEST_BEGIN();
    int rc;
    /* cell 1 wraps to a second line, cell 2 sits back on the row's top y */
    static const char ROW[] = "| aa bb | cc |";
    TextRuns r;
    text_runs_init(&r);
    add_run(&r, ROW, 2, 5, 10, 0, TR_PART_MAIN, 0);    /* "aa " */
    add_run(&r, ROW, 5, 7, 10, 12, TR_PART_MAIN, 0);   /* "bb" (wrapped) */
    add_run(&r, ROW, 10, 12, 60, 0, TR_PART_MAIN, 0);  /* "cc" */
    rc = text_runs_hit(&r, 64, 5, NULL);
    ASSERT_EQ(rc, 11);     /* inside "cc" */
    rc = text_runs_hit(&r, 40, 5, NULL);
    ASSERT_EQ(rc, 5);      /* between cells */
    rc = text_runs_hit(&r, 500, 5, NULL);
    ASSERT_EQ(rc, 12);    /* right of the row */
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_hit_empty_is_minus_one(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    text_runs_init(&r);
    rc = text_runs_hit(&r, 1, 1, NULL);
    ASSERT_EQ(rc, -1);
    rc = text_runs_hit(NULL, 1, 1, NULL);
    ASSERT_EQ(rc, -1);
    rc = text_runs_run_at(&r, 1, 1);
    ASSERT_EQ(rc, -1);
    TEST_END();
}

int test_text_runs_span_px_partial_full_and_none(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    const TextRun *t = &r.v[1];   /* "w\xC3\xB6rld" [6,12) */
    const int *c = r.cum + t->first_cum, *b = r.cum_byte + t->first_cum;
    int x0 = -1, x1 = -1;
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 0, 99, &x0, &x1);
    ASSERT_TRUE(rc);
    ASSERT_EQ(x0, 0);  ASSERT_EQ(x1, 30);
    /* from the umlaut (byte 7) to after "r" (byte 10) */
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 7, 10, &x0, &x1);
    ASSERT_TRUE(rc);
    ASSERT_EQ(x0, 6);  ASSERT_EQ(x1, 18);
    /* a range that starts inside the umlaut takes the next boundary */
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 8, 12, &x0, &x1);
    ASSERT_TRUE(rc);
    ASSERT_EQ(x0, 12); ASSERT_EQ(x1, 30);
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 0, 6, &x0, &x1);
    ASSERT_FALSE(rc);
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 12, 20, &x0, &x1);
    ASSERT_FALSE(rc);
    rc = text_runs_span_px(t->start, t->end, c, b, t->n_cum, 9, 9, &x0, &x1);
    ASSERT_FALSE(rc);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_run_at_finds_the_rect(void)
{
    TEST_BEGIN();
    int rc;
    TextRuns r;
    build_fixture(&r);
    rc = text_runs_run_at(&r, 50, 3);
    ASSERT_EQ(rc, 1);
    rc = text_runs_run_at(&r, 12, 15);
    ASSERT_EQ(rc, 2);
    rc = text_runs_run_at(&r, 300, 3);
    ASSERT_EQ(rc, -1);
    text_runs_free(&r);
    TEST_END();
}

int test_text_runs_word_prose_code_and_synthetic(void)
{
    TEST_BEGIN();
    static const char T[] = "make  all && cd /tmp";
    TextRun prose, code, syn;
    memset(&prose, 0, sizeof(prose));
    prose.start = 0; prose.end = 6;        /* "make  " */
    int s = -1, e = -1;
    text_runs_word(T, (int)strlen(T), &prose, 2, &s, &e);
    ASSERT_EQ(s, 0); ASSERT_EQ(e, 4);
    memset(&code, 0, sizeof(code));
    code.start = 0; code.end = (int)strlen(T); code.flags = TR_FLAG_CODE;
    text_runs_word(T, (int)strlen(T), &code, 10, &s, &e);   /* on "&&" */
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 12);
    text_runs_word(T, (int)strlen(T), &code, 12, &s, &e);   /* just after it */
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 12);
    text_runs_word(T, (int)strlen(T), &code, 5, &s, &e);    /* between spaces */
    ASSERT_EQ(s, 5); ASSERT_EQ(e, 5);
    memset(&syn, 0, sizeof(syn));
    syn.start = 0; syn.end = 6; strcpy(syn.synthetic, "- ");
    text_runs_word(T, (int)strlen(T), &syn, 2, &s, &e);
    ASSERT_EQ(s, e);
    TEST_END();
}

int test_text_line_at_and_next_line(void)
{
    TEST_BEGIN();
    int rc;
    static const char T[] = "one\ntwo\r\n\nfour";
    int s = -1, e = -1;
    text_line_at(T, (int)strlen(T), 5, &s, &e);
    ASSERT_EQ(s, 4); ASSERT_EQ(e, 8);          /* "two\r": the bytes as stored */
    text_line_at(T, (int)strlen(T), 9, &s, &e);
    ASSERT_EQ(s, 9); ASSERT_EQ(e, 9);          /* the empty line */
    text_line_at(T, (int)strlen(T), 99, &s, &e);
    ASSERT_EQ(s, 10); ASSERT_EQ(e, 14);
    int pos = 0, ls, le, n = 0;
    int want[4][2] = { { 0, 3 }, { 4, 7 }, { 9, 9 }, { 10, 14 } };
    while (text_next_line(T, (int)strlen(T), &pos, &ls, &le)) {
        ASSERT_TRUE(n < 4);
        if (n < 4) { ASSERT_EQ(ls, want[n][0]); ASSERT_EQ(le, want[n][1]); }
        n++;
    }
    ASSERT_EQ(n, 4);
    pos = 0; n = 0;
    while (text_next_line("a\n", 2, &pos, &ls, &le)) n++;
    ASSERT_EQ(n, 1);                           /* no line after a final '\n' */
    pos = 0;
    rc = text_next_line("x\ry", 3, &pos, &ls, &le);
    ASSERT_TRUE(rc);
    ASSERT_EQ(le, 3);                          /* a lone CR is text */
    TEST_END();
}
