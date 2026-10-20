/* tests/test_conpty_expect.c -- the console probe's expectation helper
 * (tests/conpty_expect.c). The parity case in tests/test_local_pty.c
 * compares conhost's buffer with these predictions on a real Windows host;
 * these cases pin the predictions themselves, worked by hand from
 * docs/superpowers/specs/2026-10-18-conpty-resize-anchor-design.md
 * section 1.3 and section 2. Native, `make test`. */

#include "test_framework.h"
#include "conpty_expect.h"
#include <string.h>

static void blank_view(CpxView *v, int rows, int cols)
{
    memset(v, 0, sizeof(*v));
    v->rows = rows;
    v->cols = cols;
}

int test_cpx_place_inside_and_past_content(void)
{
    TEST_BEGIN();
    int r = -1, c = -1;
    cpx_place(10, 3, 40, &r, &c);   /* inside                         */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 3);
    cpx_place(3, 3, 40, &r, &c);    /* one past "P> "                 */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 3);
    cpx_place(52, 51, 25, &r, &c);  /* last cell of a 3-row line      */
    ASSERT_EQ(r, 2); ASSERT_EQ(c, 1);
    cpx_place(7, 8, 8, &r, &c);     /* PS C:\> + 1, narrowed to 8     */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 7);
    cpx_place(7, 8, 6, &r, &c);     /* rewrapped: ">" then 1 past     */
    ASSERT_EQ(r, 1); ASSERT_EQ(c, 2);
    cpx_place(20, 20, 20, &r, &c);  /* exact width, clamped           */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 19);
    cpx_place(0, 0, 10, &r, &c);    /* empty line                     */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 0);
    cpx_place(0, 12, 10, &r, &c);   /* empty line, far past, clamped  */
    ASSERT_EQ(r, 0); ASSERT_EQ(c, 9);
    TEST_END();
}

/* Four scenarios in a fixed order, each findable by name, none with a line
 * whose length is a multiple of its old width (see conpty_expect.c). */
int test_cpx_scenarios_are_listed_and_writable(void)
{
    TEST_BEGIN();
    static const char *NAMES[] = { "grow", "shrink_bottom", "shrink_below",
                                   "width" };
    ASSERT_EQ(cpx_scenario_count(), 4);
    for (int i = 0; i < 4; i++) {
        const CpxScenario *s = cpx_scenario_at(i);
        ASSERT_NOT_NULL(s);
        if (!s) continue;
        ASSERT_STR_EQ(s->name, NAMES[i]);
        ASSERT_TRUE(cpx_scenario_find(NAMES[i]) == s);
        for (int l = 0; l < s->line_count; l++) {
            size_t len = strlen(s->lines[l]);
            ASSERT_TRUE(len == 0u || len % (size_t)s->old_cols != 0u);
        }
        /* tags are unique */
        for (int a = 0; a < s->line_count; a++) {
            char ta[16], tb[16];
            size_t na = cpx_tag(s->lines[a], ta, sizeof(ta));
            if (na == 0u) continue;
            for (int b = a + 1; b < s->line_count; b++) {
                size_t nb = cpx_tag(s->lines[b], tb, sizeof(tb));
                if (nb == 0u) continue;
                ASSERT_TRUE(strcmp(ta, tb) != 0);
            }
        }
    }
    ASSERT_NULL(cpx_scenario_at(4));
    ASSERT_NULL(cpx_scenario_at(-1));
    ASSERT_NULL(cpx_scenario_find("nope"));
    ASSERT_NULL(cpx_scenario_find(NULL));
    TEST_END();
}

/* grow, 40x12 -> 40x20: <10>..<20> and the prompt stay on rows 0-11, the
 * cursor stays on row 11, rows 12-19 are blank -- nothing pulled down. */
int test_cpx_grow_keeps_the_top_line(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("grow");
    CpxView v;
    ASSERT_NOT_NULL(s);
    int rc = cpx_initial_view(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(v.text[0], "<10> alpha");
    ASSERT_STR_EQ(v.text[10], "<20> alpha");
    ASSERT_STR_EQ(v.text[11], "P>");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);

    rc = cpx_expected_after(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(v.rows, 20);
    ASSERT_EQ(v.cols, 40);
    ASSERT_STR_EQ(v.text[0], "<10> alpha");
    ASSERT_STR_EQ(v.text[11], "P>");
    for (int r = 12; r < 20; r++) ASSERT_STR_EQ(v.text[r], "");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* shrink_bottom, 40x12 -> 40x8: four rows leave the top. */
int test_cpx_shrink_with_cursor_at_bottom_drops_from_the_top(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("shrink_bottom");
    CpxView v;
    ASSERT_NOT_NULL(s);
    int rc = cpx_expected_after(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(v.rows, 8);
    ASSERT_STR_EQ(v.text[0], "<14> alpha");
    ASSERT_STR_EQ(v.text[6], "<20> alpha");
    ASSERT_STR_EQ(v.text[7], "P>");
    ASSERT_EQ(v.cursor_row, 7);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* shrink_below, 40x12 -> 40x8: the segment runs to <B> on row 10, eleven
 * rows, so three leave the top and the cursor follows its row to 3. */
int test_cpx_shrink_with_content_below_counts_it(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("shrink_below");
    CpxView v;
    ASSERT_NOT_NULL(s);
    int rc = cpx_initial_view(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(v.text[6], "P>");
    ASSERT_STR_EQ(v.text[10], "<B> below");
    ASSERT_EQ(v.cursor_row, 6);

    rc = cpx_expected_after(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(v.text[0], "<04> alpha");
    ASSERT_STR_EQ(v.text[2], "<06> alpha");
    ASSERT_STR_EQ(v.text[3], "P>");
    ASSERT_STR_EQ(v.text[4], "");
    ASSERT_STR_EQ(v.text[6], "");
    ASSERT_STR_EQ(v.text[7], "<B> below");
    ASSERT_EQ(v.cursor_row, 3);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* width, 40x12 -> 25x12: every line rewraps, sixteen rows, four leave the
 * top (<W1> and <W2>, two rows each). */
int test_cpx_width_change_rewraps_and_overflows(void)
{
    TEST_BEGIN();
    const CpxScenario *s = cpx_scenario_find("width");
    CpxView v;
    ASSERT_NOT_NULL(s);
    int rc = cpx_initial_view(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(v.text[3], "<WL> abcdefghijklmnopqrstuvwxyzABCDEFGHI");
    ASSERT_STR_EQ(v.text[4], "JKLMNOPQRSTU");
    ASSERT_STR_EQ(v.text[8], "P>");
    ASSERT_EQ(v.cursor_row, 8);

    rc = cpx_expected_after(s, &v);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(v.cols, 25);
    ASSERT_STR_EQ(v.text[0], "<W3> abcdefghijklmnopqrst");
    ASSERT_STR_EQ(v.text[1], "uvwxyz012");
    ASSERT_STR_EQ(v.text[2], "<WL> abcdefghijklmnopqrst");
    ASSERT_STR_EQ(v.text[3], "uvwxyzABCDEFGHIJKLMNOPQRS");
    ASSERT_STR_EQ(v.text[4], "TU");
    ASSERT_STR_EQ(v.text[10], "uvwxyz012");
    ASSERT_STR_EQ(v.text[11], "P>");
    ASSERT_EQ(v.cursor_row, 11);
    ASSERT_EQ(v.cursor_col, 3);
    TEST_END();
}

/* A line straddling conhost's top cannot be predicted (its head is gone):
 * -1, not a guess. Bad sizes are -1 too. */
int test_cpx_rejects_unpredictable_scenarios(void)
{
    TEST_BEGIN();
    CpxScenario s;
    CpxView v;
    memset(&s, 0, sizeof(s));
    s.name = "straddle";
    s.old_cols = 10; s.old_rows = 3; s.new_cols = 10; s.new_rows = 5;
    s.line_count = 3;
    s.lines[0] = "<1> aaaaaaaaaaaa";   /* 16 cells: two rows at 10 */
    s.lines[1] = "<2>";
    s.lines[2] = "<3>";
    s.cursor_line = 2; s.cursor_offset = 3;
    int rc = cpx_initial_view(&s, &v);
    ASSERT_EQ(rc, -1);
    rc = cpx_expected_after(&s, &v);
    ASSERT_EQ(rc, -1);

    s.lines[0] = "<1>";
    rc = cpx_initial_view(&s, &v);
    ASSERT_EQ(rc, 0);
    s.new_cols = CPX_MAX_COLS + 1;
    rc = cpx_expected_after(&s, &v);
    ASSERT_EQ(rc, -1);
    rc = cpx_initial_view(NULL, &v);
    ASSERT_EQ(rc, -1);
    TEST_END();
}

int test_cpx_view_diff_counts_rows_cursor_and_size(void)
{
    TEST_BEGIN();
    CpxView a, b;
    blank_view(&a, 3, 10);
    blank_view(&b, 3, 10);
    int d = cpx_view_diff(&a, &b);
    ASSERT_EQ(d, 0);
    (void)strcpy(b.text[1], "x");
    d = cpx_view_diff(&a, &b);
    ASSERT_EQ(d, 1);
    b.cursor_col = 2;
    d = cpx_view_diff(&a, &b);
    ASSERT_EQ(d, 2);
    b.rows = 4;
    (void)strcpy(b.text[3], "y");
    d = cpx_view_diff(&a, &b);
    ASSERT_EQ(d, 4);
    char tag[8];
    size_t n = cpx_tag("<07> alpha", tag, sizeof(tag));
    ASSERT_EQ(n, 4u);
    ASSERT_STR_EQ(tag, "<07>");
    n = cpx_tag("", tag, sizeof(tag));
    ASSERT_EQ(n, 0u);
    n = cpx_tag("P> ", tag, sizeof(tag));
    ASSERT_EQ(n, 2u);
    ASSERT_STR_EQ(tag, "P>");
    TEST_END();
}
