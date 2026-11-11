/* Lazy scrollback rows (spec 2026-11-05-settings-help-design.md, item C): the
 * ring keeps its size and indexing, but only the live part holds rows. The
 * allocator behind every fallible allocation is replaced through
 * term_set_calloc() so a test can count calls and fail the n-th one. */
#include "test_framework.h"
#include "term.h"
#include "selection.h"
#include <stdlib.h>
#include <string.h>

static int g_calls, g_fail_at;   /* g_fail_at: 0 never fails, -1 always fails, n > 0 fails the n-th call */
static void *fake_calloc(size_t n, size_t sz)
{
    g_calls++;
    if (g_fail_at == -1 || g_calls == g_fail_at) return NULL;
    return calloc(n, sz);
}
static void hook(int fail_at) { g_calls = 0; g_fail_at = fail_at; term_set_calloc(fake_calloc); }
static void unhook(void) { term_set_calloc(NULL); }

/* Exactly the live part [start, start + live) mod cap holds rows, where live is
 * term_logical_rows(): lines_count, or rows in a sparse state a test forced. */
static int seg_ok(TermRow *const *lines, int cap, int start, int count)
{
    for (int i = 0; i < cap; i++) {
        int in = ((i - start + cap) % cap) < count;
        if (in != (lines[i] != NULL)) return 0;
    }
    return 1;
}
static int ring_ok(const Terminal *t)
{
    if (!seg_ok(t->lines, t->lines_capacity, t->lines_start, term_logical_rows(t))) return 0;
    if (t->alt_screen_active &&
        !seg_ok(t->primary_lines, t->primary_lines_capacity,
                t->primary_lines_start,
                t->primary_lines_count > t->rows ? t->primary_lines_count : t->rows)) return 0;
    return 1;
}
static int rows_held(const Terminal *t)
{
    int n = 0;
    for (int i = 0; i < t->lines_capacity; i++) n += t->lines[i] != NULL;
    return n;
}
static void feed(Terminal *t, const char *s) { term_process(t, s, strlen(s)); }
static char *all_text(const Terminal *t)
{
    Selection s; size_t n = 0;
    selection_select_all(&s, t);
    return selection_copy_text(&s, t, &n);
}
/* The row at logical index i of the live ring. */
static const TermRow *lrow(const Terminal *t, int i)
{
    return t->lines[(t->lines_start + i) % t->lines_capacity];
}

/* term_init(5, 10, 100) holding twelve 10-character lines and one of 25. */
static Terminal *make_partly(void)
{
    Terminal *t = term_init(5, 10, 100);
    for (int i = 0; i < 12; i++) {
        char line[24];
        snprintf(line, sizeof line, "row%02d.abcd\r\n", i);
        feed(t, line);
    }
    feed(t, "0123456789abcdefghijKLMNO");
    return t;
}

int test_term_sb_fresh_tab_rows_only(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(50, 200, 10000);
    ASSERT_EQ(rows_held(t), 50);
    ASSERT_EQ(t->lines_capacity, 10050);
    /* These two asserts document the arithmetic behind the spec's memory
     * figures (the slots plus 50 rows of 200 cells) and test no behaviour. */
    ASSERT_EQ((size_t)t->lines_capacity * sizeof(TermRow *) +
              50 * (sizeof(TermRow) + 200 * sizeof(TermCell)), (size_t)282000);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);

    t = term_init(50, 200, 50000);
    ASSERT_EQ(rows_held(t), 50);
    ASSERT_EQ(t->lines_capacity, 50050);
    ASSERT_EQ((size_t)t->lines_capacity * sizeof(TermRow *) +
              50 * (sizeof(TermRow) + 200 * sizeof(TermCell)), (size_t)602000);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_grows_one_row_per_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "\n\n\n");                      /* cursor reaches the bottom row */
    ASSERT_EQ(rows_held(t), 4); ASSERT_TRUE(ring_ok(t));
    for (int k = 1; k <= 10; k++) {
        feed(t, "\n");
        ASSERT_EQ(t->lines_count, 4 + k);
        ASSERT_EQ(rows_held(t), 4 + k);
        ASSERT_TRUE(ring_ok(t));
    }
    term_free(t);
    TEST_END();
}

int test_term_sb_full_ring_recycles_without_alloc(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 6);
    for (int i = 1; i <= 30; i++) {
        char line[16];
        snprintf(line, sizeof line, "L%02d\r\n", i);
        feed(t, line);
    }
    ASSERT_EQ(t->lines_count, 10);
    ASSERT_EQ(rows_held(t), 10);
    ASSERT_EQ(t->lines_recycled, (uint64_t)21);
    const TermRow *oldest = t->lines[t->lines_start];
    ASSERT_NOT_NULL(oldest);
    if (oldest) {
        ASSERT_EQ(oldest->cells[0].codepoint, (uint32_t)'L');
        ASSERT_EQ(oldest->cells[1].codepoint, (uint32_t)'2');
        ASSERT_EQ(oldest->cells[2].codepoint, (uint32_t)'2');
    }
    ASSERT_TRUE(ring_ok(t));
    hook(0);
    for (int i = 0; i < 10; i++) feed(t, "more\r\n");
    ASSERT_EQ(g_calls, 0);
    ASSERT_TRUE(ring_ok(t));
    unhook();
    term_free(t);
    TEST_END();
}

int test_term_sb_resize_partly_filled(void)
{
    TEST_BEGIN();
    Terminal *t = make_partly();
    char *before = all_text(t);
    ASSERT_NOT_NULL(before);
    ASSERT_TRUE(ring_ok(t));
    ASSERT_EQ(rows_held(t), t->lines_count);

    static const int sizes[4][2] = { {5, 20}, {5, 4}, {8, 10}, {3, 10} };
    for (int k = 0; k < 4; k++) {
        ASSERT_TRUE(term_resize(t, sizes[k][0], sizes[k][1]));
        ASSERT_TRUE(ring_ok(t));
        ASSERT_EQ(rows_held(t), t->lines_count);
        ASSERT_TRUE(t->lines_count <= t->lines_capacity);
        char *txt = all_text(t);
        ASSERT_NOT_NULL(txt);
        if (txt && before) ASSERT_STR_EQ(txt, before);
        free(txt);
    }
    free(before);
    term_free(t);

    /* The padding edge: a 3-row terminal with one line of scrollback, two
     * lines held, resized to 6 rows. The padding slots overlap the spare
     * rows (capacity 7, six rows allocated, four padding slots at 3..6). */
    t = term_init(3, 10, 1);
    feed(t, "ab\r\ncd");
    before = all_text(t);
    ASSERT_TRUE(term_resize(t, 6, 10));
    ASSERT_TRUE(ring_ok(t));
    ASSERT_EQ(rows_held(t), t->lines_count);
    ASSERT_EQ(t->lines_count, 6);
    /* Growing pads blank rows above the content, so the text is the old text
     * under four blank lines (6 rows - the 2 the text needs). */
    char *txt = all_text(t);
    ASSERT_NOT_NULL(txt);
    if (txt && before) {
        char want[64];
        snprintf(want, sizeof want, "\r\n\r\n\r\n\r\n%s", before);
        ASSERT_STR_EQ(txt, want);
    }
    ASSERT_EQ(lrow(t, 4)->cells[0].codepoint, (uint32_t)'a');
    ASSERT_EQ(lrow(t, 5)->cells[0].codepoint, (uint32_t)'c');
    free(txt);
    free(before);
    term_free(t);
    TEST_END();
}

int test_term_sb_resize_alt_screen_partly_filled(void)
{
    TEST_BEGIN();
    Terminal *t = make_partly();
    char *before = all_text(t);
    ASSERT_NOT_NULL(before);

    feed(t, "\x1b[?1049h");
    ASSERT_TRUE(t->alt_screen_active);
    ASSERT_TRUE(ring_ok(t));
    ASSERT_TRUE(term_resize(t, 5, 20));
    ASSERT_TRUE(ring_ok(t));
    ASSERT_TRUE(t->alt_screen_active);
    feed(t, "\x1b[?1049l");
    ASSERT_FALSE(t->alt_screen_active);
    ASSERT_TRUE(ring_ok(t));
    ASSERT_EQ(rows_held(t), t->lines_count);

    char *txt = all_text(t);
    ASSERT_NOT_NULL(txt);
    if (txt && before) ASSERT_STR_EQ(txt, before);
    free(txt);
    free(before);
    term_free(t);
    TEST_END();
}

int test_term_sb_grow_alloc_failure_recycles(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    hook(-1);
    for (int k = 0; k < 10; k++) { char line[8]; snprintf(line, sizeof line, "\r\nX%d", k); feed(t, line); }
    ASSERT_EQ(t->lines_count, 4); ASSERT_EQ(rows_held(t), 4);
    ASSERT_EQ(t->lines_recycled, (uint64_t)10);
    ASSERT_TRUE(t->grow_failing); ASSERT_TRUE(ring_ok(t));
    char *txt = all_text(t);
    ASSERT_NOT_NULL(txt);
    if (txt) ASSERT_STR_EQ(txt, "X6\r\nX7\r\nX8\r\nX9");   /* selection_copy_text joins with CRLF */
    free(txt);
    unhook();
    feed(t, "\r\n");
    ASSERT_EQ(t->lines_count, 5); ASSERT_FALSE(t->grow_failing); ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_resize_clears_grow_failing(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    hook(-1);
    feed(t, "\r\nE");
    ASSERT_TRUE(t->grow_failing);
    unhook();
    /* A committed resize ends the failing streak, so a later one logs again. */
    ASSERT_TRUE(term_resize(t, 4, 12));
    ASSERT_FALSE(t->grow_failing);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_grow_cells_failure_no_leak(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    hook(2);                                /* the cells block of the first new row fails */
    feed(t, "\n");
    ASSERT_EQ(t->lines_count, 4);
    ASSERT_TRUE(t->grow_failing);
    ASSERT_EQ(rows_held(t), 4);
    ASSERT_TRUE(ring_ok(t));
    unhook();
    term_free(t);                           /* LeakSanitizer is the check (make test SAN=1) */
    TEST_END();
}

int test_term_sb_resize_alloc_failure_unchanged(void)
{
    TEST_BEGIN();
    /* Learn how many allocator calls a 5x4 reflow of this terminal makes. */
    Terminal *probe = make_partly();
    hook(0);
    ASSERT_TRUE(term_resize(probe, 5, 4));
    int n = g_calls;
    unhook();
    term_free(probe);
    ASSERT_TRUE(n > 4);

    int fails[4] = { 1, 2, n / 2, n };
    for (int k = 0; k < 4; k++) {
        Terminal *t = make_partly();
        t->erase_seq = 7;                  /* values a failed resize must not touch */
        int rows = t->rows, cols = t->cols, count = t->lines_count, start = t->lines_start;
        uint64_t recycled = t->lines_recycled;
        uint32_t epoch = t->epoch, reflow = t->reflow_seq, erase = t->erase_seq;
        TermCursor cur = t->cursor;
        char *before = all_text(t);
        ASSERT_NOT_NULL(before);

        hook(fails[k]);
        ASSERT_FALSE(term_resize(t, 5, 4));
        unhook();
        ASSERT_EQ(t->rows, rows); ASSERT_EQ(t->cols, cols);
        ASSERT_EQ(t->lines_count, count); ASSERT_EQ(t->lines_start, start);
        ASSERT_EQ(t->lines_recycled, recycled); ASSERT_EQ(t->epoch, epoch);
        ASSERT_EQ(t->reflow_seq, reflow);
        ASSERT_EQ(t->erase_seq, erase);
        ASSERT_EQ(t->cursor.row, cur.row); ASSERT_EQ(t->cursor.col, cur.col);
        ASSERT_TRUE(ring_ok(t));
        char *txt = all_text(t);
        ASSERT_NOT_NULL(txt);
        if (txt && before) ASSERT_STR_EQ(txt, before);
        free(txt);
        free(before);
        ASSERT_TRUE(t->resize_failing);

        if (k == 3) {
            /* The flag stays through a second failing resize, and clears on
             * the next one that works. */
            hook(1);
            ASSERT_FALSE(term_resize(t, 5, 4));
            unhook();
            ASSERT_TRUE(t->resize_failing);
            ASSERT_TRUE(term_resize(t, 5, 4));
            ASSERT_FALSE(t->resize_failing);
            ASSERT_EQ(t->cols, 4);
            ASSERT_TRUE(ring_ok(t));
        }
        term_free(t);
    }
    TEST_END();
}

int test_term_sb_resize_alt_alloc_failure_unchanged(void)
{
    TEST_BEGIN();
    /* Count the calls of a successful resize of an identical terminal. The
     * active (alternate) ring is reflowed first, so the last call belongs to
     * the primary's reflow. */
    Terminal *probe = make_partly();
    feed(probe, "\x1b[?1049h");
    hook(0);
    ASSERT_TRUE(term_resize(probe, 5, 4));
    int n = g_calls;
    unhook();
    term_free(probe);
    ASSERT_TRUE(n > 4);

    Terminal *t = make_partly();
    char *primary_before = all_text(t);
    feed(t, "\x1b[?1049h");
    feed(t, "alt text");
    ASSERT_TRUE(t->alt_screen_active);

    TermRow **lines = t->lines, **plines = t->primary_lines;
    int cap = t->lines_capacity, pcap = t->primary_lines_capacity;
    int count = t->lines_count, pcount = t->primary_lines_count;
    int start = t->lines_start, pstart = t->primary_lines_start;
    TermCursor cur = t->cursor, pcur = t->primary_cursor;
    uint32_t epoch = t->epoch, reflow = t->reflow_seq;
    uint64_t recycled = t->lines_recycled, precycled = t->primary_lines_recycled;
    char *alt_before = all_text(t);

    hook(n);
    ASSERT_FALSE(term_resize(t, 5, 4));
    unhook();
    ASSERT_TRUE(t->alt_screen_active);
    ASSERT_EQ(t->rows, 5); ASSERT_EQ(t->cols, 10);
    ASSERT_TRUE(t->lines == lines); ASSERT_TRUE(t->primary_lines == plines);
    ASSERT_EQ(t->lines_capacity, cap); ASSERT_EQ(t->primary_lines_capacity, pcap);
    ASSERT_EQ(t->lines_count, count); ASSERT_EQ(t->primary_lines_count, pcount);
    ASSERT_EQ(t->lines_start, start); ASSERT_EQ(t->primary_lines_start, pstart);
    ASSERT_EQ(t->cursor.row, cur.row); ASSERT_EQ(t->cursor.col, cur.col);
    ASSERT_EQ(t->primary_cursor.row, pcur.row); ASSERT_EQ(t->primary_cursor.col, pcur.col);
    ASSERT_EQ(t->epoch, epoch);
    ASSERT_EQ(t->reflow_seq, reflow);
    ASSERT_EQ(t->lines_recycled, recycled); ASSERT_EQ(t->primary_lines_recycled, precycled);
    ASSERT_TRUE(t->resize_failing);
    ASSERT_TRUE(ring_ok(t));

    char *alt_after = all_text(t);
    ASSERT_NOT_NULL(alt_after); ASSERT_NOT_NULL(alt_before);
    if (alt_after && alt_before) ASSERT_STR_EQ(alt_after, alt_before);
    free(alt_after); free(alt_before);

    feed(t, "\x1b[?1049l");
    char *primary_after = all_text(t);
    ASSERT_NOT_NULL(primary_after); ASSERT_NOT_NULL(primary_before);
    if (primary_after && primary_before) ASSERT_STR_EQ(primary_after, primary_before);
    free(primary_after); free(primary_before);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_alt_enter_alloc_failure_stays_primary(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    uint32_t epoch = t->epoch;
    hook(-1);
    feed(t, "\x1b[?1049h");
    unhook();
    ASSERT_FALSE(t->alt_screen_active);
    ASSERT_EQ(t->epoch, epoch);
    ASSERT_TRUE(ring_ok(t));
    feed(t, "hello");
    const TermRow *row = t->lines[term_screen_to_phys(t, t->cursor.row)];
    ASSERT_NOT_NULL(row);
    if (row) {
        ASSERT_EQ(row->cells[0].codepoint, (uint32_t)'h');
        ASSERT_EQ(row->cells[4].codepoint, (uint32_t)'o');
    }
    int count = t->lines_count;
    TermCursor cur = t->cursor;
    epoch = t->epoch;
    feed(t, "\x1b[?1049l");
    ASSERT_FALSE(t->alt_screen_active);
    ASSERT_EQ(t->lines_count, count);
    ASSERT_EQ(t->cursor.row, cur.row); ASSERT_EQ(t->cursor.col, cur.col);
    ASSERT_EQ(t->epoch, epoch);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_delete_lines_at_top_grows(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    feed(t, "\x1b[H\x1b[2M");   /* CSI M on row 0, the region the whole screen */
    ASSERT_EQ(t->lines_count, 6);
    ASSERT_EQ(rows_held(t), 6);
    ASSERT_EQ(lrow(t, 0)->cells[0].codepoint, (uint32_t)'A');
    ASSERT_EQ(lrow(t, 1)->cells[0].codepoint, (uint32_t)'B');
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_wrapped_live_part_resizes(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(5, 10, 100);
    ASSERT_EQ(t->lines_capacity, 105);
    feed(t, "A\r\nB\r\nC");
    ASSERT_TRUE(term_resize(t, 10, 10));
    ASSERT_EQ(t->lines_capacity, 110);
    ASSERT_EQ(t->lines_start, 103);
    ASSERT_EQ(t->lines_count, 10);
    ASSERT_NOT_NULL(t->lines[103]); ASSERT_NOT_NULL(t->lines[109]);
    ASSERT_NOT_NULL(t->lines[0]);   ASSERT_NOT_NULL(t->lines[2]);
    ASSERT_NULL(t->lines[3]);       ASSERT_NULL(t->lines[102]);
    ASSERT_TRUE(ring_ok(t));

    for (int i = 0; i < 20; i++) feed(t, "\n");   /* the cursor is on the bottom row */
    ASSERT_EQ(t->lines_count, 30);
    ASSERT_NOT_NULL(t->lines[3]); ASSERT_NOT_NULL(t->lines[22]);
    ASSERT_NULL(t->lines[23]);    ASSERT_NULL(t->lines[102]);
    ASSERT_TRUE(ring_ok(t));

    ASSERT_TRUE(term_resize(t, 5, 4));
    ASSERT_EQ(t->lines_capacity, 105);
    ASSERT_EQ(t->lines_start, 0);
    ASSERT_EQ(t->lines_count, 30);
    ASSERT_TRUE(ring_ok(t));
    ASSERT_EQ(lrow(t, 7)->cells[0].codepoint, (uint32_t)'A');
    ASSERT_EQ(lrow(t, 8)->cells[0].codepoint, (uint32_t)'B');
    ASSERT_EQ(lrow(t, 9)->cells[0].codepoint, (uint32_t)'C');
    ASSERT_EQ(t->cursor.row, 4);
    term_free(t);
    TEST_END();
}

/* L0..L9 on a 4-row, 10-column terminal with room for 100 lines of history. */
static Terminal *make_l0_l9(void)
{
    Terminal *t = term_init(4, 10, 100);
    feed(t, "L0\r\nL1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\nL7\r\nL8\r\nL9");
    return t;
}

/* Does logical row i read as the two characters c0 c1? */
static int lrow_is(const Terminal *t, int i, char c0, char c1)
{
    const TermRow *r = lrow(t, i);
    return r && r->cells[0].codepoint == (uint32_t)c0 && r->cells[1].codepoint == (uint32_t)c1;
}

int test_term_sb_grow_failure_with_history_then_resize(void)
{
    TEST_BEGIN();
    Terminal *t = make_l0_l9();
    ASSERT_EQ(t->lines_count, 10);
    ASSERT_EQ(t->lines_start, 0);
    hook(-1);
    for (int k = 0; k < 5; k++) { char s[16]; snprintf(s, sizeof s, "\r\nM%d", k); feed(t, s); }
    unhook();
    ASSERT_EQ(t->lines_start, 5);
    for (int i = 0; i < 5; i++) ASSERT_NULL(t->lines[i]);
    ASSERT_EQ(t->lines_recycled, (uint64_t)5);
    ASSERT_TRUE(ring_ok(t));
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(lrow_is(t, i, 'L', (char)('5' + i)));
        ASSERT_TRUE(lrow_is(t, 5 + i, 'M', (char)('0' + i)));
    }

    ASSERT_TRUE(term_resize(t, 4, 20));
    ASSERT_EQ(t->lines_start, 0);
    ASSERT_EQ(t->lines_count, 10);
    ASSERT_EQ(t->lines_recycled, (uint64_t)5);
    ASSERT_TRUE(ring_ok(t));
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(lrow_is(t, i, 'L', (char)('5' + i)));
        ASSERT_TRUE(lrow_is(t, 5 + i, 'M', (char)('0' + i)));
    }
    term_free(t);
    TEST_END();
}

int test_term_sb_grow_failure_scrolled_back(void)
{
    TEST_BEGIN();
    Terminal *t = make_l0_l9();
    t->scrollback_offset = 3;
    ASSERT_TRUE(lrow_is(t, term_view_to_logical(t, 0), 'L', '3'));
    hook(-1);
    feed(t, "\r\nM0\r\nM1");
    unhook();
    ASSERT_EQ(t->scrollback_offset, 5);
    ASSERT_TRUE(lrow_is(t, term_view_to_logical(t, 0), 'L', '3'));
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_grow_failure_selection_matches_full_ring(void)
{
    TEST_BEGIN();
    Terminal *a = make_l0_l9();                 /* room to grow */
    Terminal *b = term_init(4, 10, 6);          /* capacity 10: exactly full after L0..L9 */
    feed(b, "L0\r\nL1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\nL7\r\nL8\r\nL9");
    ASSERT_EQ(b->lines_count, 10);
    ASSERT_EQ(b->lines_count, b->lines_capacity);

    Selection sa, sb;
    selection_select_all(&sa, a);
    selection_select_all(&sb, b);
    ASSERT_TRUE(sa.valid); ASSERT_TRUE(sb.valid);

    hook(-1);                                   /* every allocation fails: a drops L0 */
    feed(a, "\r\nM0");
    unhook();
    feed(b, "\r\nM0");                          /* b is full: it recycles L0 */
    ASSERT_TRUE(a->grow_failing);
    ASSERT_EQ(a->lines_recycled, b->lines_recycled);
    ASSERT_EQ(a->lines_count, b->lines_count);

    bool ma = selection_sync(&sa, a);
    bool mb = selection_sync(&sb, b);
    ASSERT_EQ(ma, mb);
    ASSERT_EQ(sa.valid, sb.valid);
    ASSERT_EQ(sa.start.line, sb.start.line); ASSERT_EQ(sa.start.col, sb.start.col);
    ASSERT_EQ(sa.end.line, sb.end.line);     ASSERT_EQ(sa.end.col, sb.end.col);

    size_t la = 0, lb = 0;
    char *ta = selection_copy_text(&sa, a, &la);
    char *tb = selection_copy_text(&sb, b, &lb);
    ASSERT_NOT_NULL(ta); ASSERT_NOT_NULL(tb);
    ASSERT_EQ(la, lb);
    if (ta && tb) ASSERT_STR_EQ(ta, tb);
    /* Not an empty comparison: L0 is gone from both, the rest is selected. */
    ASSERT_TRUE(sa.valid);
    if (ta) ASSERT_STR_EQ(ta, "L1\r\nL2\r\nL3\r\nL4\r\nL5\r\nL6\r\nL7\r\nL8\r\nL9");
    int selected_cells = 0;
    free(ta); free(tb);

    for (int r = 0; r < a->rows; r++)
        for (int c = 0; c < a->cols; c++)
        {
            bool in_a = selection_contains(&sa, a, r, c);
            ASSERT_EQ(in_a, selection_contains(&sb, b, r, c));
            selected_cells += in_a;
        }
    ASSERT_TRUE(selected_cells > 0);
    term_free(a);
    term_free(b);
    TEST_END();
}

int test_term_sb_guard_select_all_null_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB");
    int p = term_screen_to_phys(t, 3);
    TermRow *saved = t->lines[p];
    t->lines[p] = NULL;                    /* a constructed state: unreachable through the API */
    Selection s;
    selection_select_all(&s, t);
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.end.line, t->lines_recycled + 1);   /* the row holding B */
    t->lines[p] = saved;
    term_free(t);
    TEST_END();
}

int test_term_sb_guard_scroll_dirty_null_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    int p = term_screen_to_phys(t, 1);
    TermRow *saved = t->lines[p];
    t->lines[p] = NULL;
    feed(t, "\n");                         /* the bottom row: the ring grows */
    ASSERT_EQ(t->lines_count, 5);
    ASSERT_NULL(t->lines[p]);
    t->lines[p] = saved;
    term_free(t);
    TEST_END();
}

int test_term_sb_guard_region_scroll_null_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    feed(t, "A\r\nB\r\nC\r\nD");
    int p = term_screen_to_phys(t, 1);
    TermRow *saved = t->lines[p];
    t->lines[p] = NULL;
    feed(t, "\x1b[2;3r\x1b[1S\x1b[1T");   /* scroll rows 2 and 3 up, then down */
    int nulls = 0, where = -1;
    for (int r = 0; r < t->rows; r++) {
        int idx = term_screen_to_phys(t, r);
        if (!t->lines[idx]) { nulls++; where = idx; }
    }
    ASSERT_EQ(nulls, 1);
    if (where >= 0) t->lines[where] = saved;
    term_free(t);
    TEST_END();
}

int test_term_sb_clear_keeps_rows(void)
{
    TEST_BEGIN();
    Terminal *t = make_partly();
    int count = t->lines_count, held = rows_held(t);
    hook(0);
    feed(t, "\x1b[2J");
    feed(t, "\x1b[3J");
    ASSERT_EQ(g_calls, 0);
    unhook();
    ASSERT_EQ(t->lines_count, count);
    ASSERT_EQ(rows_held(t), held);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_limit_100(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    for (int i = 0; i < 300; i++) feed(t, "line\r\n");
    ASSERT_EQ(rows_held(t), 124);
    ASSERT_EQ(t->lines_capacity, 124);
    ASSERT_EQ(term_max_scroll_offset(t), 100);
    ASSERT_TRUE(ring_ok(t));
    term_free(t);
    TEST_END();
}

int test_term_sb_limit_50000(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 2, 50000);   /* 2 columns keep the full buffer near 4 MB */
    ASSERT_EQ(rows_held(t), 24);
    size_t n = 50100;
    char *buf = malloc(n * 3);
    ASSERT_NOT_NULL(buf);
    if (!buf) { term_free(t); TEST_END(); }
    for (size_t i = 0; i < n; i++) memcpy(buf + i * 3, "x\r\n", 3);
    term_process(t, buf, n * 3);
    free(buf);
    ASSERT_EQ(rows_held(t), 50024);
    ASSERT_EQ(t->lines_capacity, 50024);
    ASSERT_EQ(term_max_scroll_offset(t), 50000);
    ASSERT_TRUE(ring_ok(t));
    hook(0);
    for (int i = 0; i < 100; i++) feed(t, "x\r\n");
    ASSERT_EQ(g_calls, 0);
    unhook();
    term_free(t);
    TEST_END();
}

int test_term_sb_free_all_states(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);                /* fresh */
    term_free(t);

    t = make_partly();                                  /* partly filled */
    term_free(t);

    t = term_init(4, 10, 6);                            /* full */
    for (int i = 0; i < 30; i++) feed(t, "full\r\n");
    ASSERT_EQ(t->lines_count, t->lines_capacity);
    term_free(t);

    t = make_partly();                                  /* alternate screen over a partly filled primary */
    feed(t, "\x1b[?1049h");
    ASSERT_TRUE(t->alt_screen_active);
    feed(t, "alt");
    term_free(t);
    TEST_END();
}
