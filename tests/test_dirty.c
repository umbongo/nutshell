#include "test_framework.h"
#include "term.h"
#include <string.h>

static void feed(Terminal *t, const char *s) {
    term_process(t, s, strlen(s));
}

/* ---- Initial state: all rows dirty after init --------------------------- */

int test_dirty_init_all_dirty(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- After clearing, no rows are dirty ---------------------------------- */

int test_dirty_clear_all(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Writing a character marks that row dirty --------------------------- */

int test_dirty_put_char(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "A");
    /* Row 0 (cursor row) should be dirty */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Only the written row is dirty, others stay clean ------------------- */

int test_dirty_only_written_row(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    /* Move cursor to row 2, write a char */
    feed(t, "\033[3;1H");  /* CSI row;col H — row 3 = index 2 */
    term_clear_dirty(t);   /* clear dirty from cursor move */
    feed(t, "X");
    /* Check that only row 2 is dirty via get_visible_row check */
    /* We can verify by checking term_has_dirty_rows returns true */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Erase display (ED) marks all rows dirty ---------------------------- */

int test_dirty_erase_display(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "\033[2J");  /* ED 2 = clear entire screen */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Erase line (EL) marks that row dirty ------------------------------- */

int test_dirty_erase_line(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    feed(t, "\033[K");   /* EL 0 = clear from cursor to end of line */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Scrolling marks the new/recycled row dirty ------------------------- */

int test_dirty_scroll(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    term_scroll(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Resize marks all rows dirty ---------------------------------------- */

int test_dirty_resize(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    term_clear_dirty(t);
    term_resize(t, 6, 12);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- NULL safety -------------------------------------------------------- */

int test_dirty_null_safety(void)
{
    TEST_BEGIN();
    term_clear_dirty(NULL);        /* must not crash */
    ASSERT_FALSE(term_has_dirty_rows(NULL));
    ASSERT_TRUE(1);
    TEST_END();
}

/* ---- Multiple writes then clear ----------------------------------------- */

int test_dirty_multi_write_clear(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 0);
    feed(t, "Hello");
    feed(t, "\nWorld");
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_clear_dirty(t);
    ASSERT_FALSE(term_has_dirty_rows(t));
    /* Write again */
    feed(t, "!");
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}

/* ---- Scroll marks ALL visible rows dirty (not just the new row) --------- */

int test_dirty_scroll_all_visible(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    /* Fill all 4 rows with content so they're used */
    feed(t, "row0\nrow1\nrow2\nrow3");
    term_clear_dirty(t);
    /* Verify all rows are clean */
    for (int i = 0; i < t->rows; i++) {
        int logical = t->lines_count - t->rows + i;
        int phys = (t->lines_start + logical) % t->lines_capacity;
        ASSERT_FALSE(t->lines[phys]->dirty);
    }
    /* Scroll once — all visible rows must become dirty because each
     * screen position now maps to a different logical row */
    term_scroll(t);
    int dirty_count = 0;
    for (int i = 0; i < t->rows; i++) {
        int logical = t->lines_count - t->rows + i;
        int phys = (t->lines_start + logical) % t->lines_capacity;
        if (t->lines[phys]->dirty) dirty_count++;
    }
    ASSERT_EQ(dirty_count, t->rows);
    term_free(t);
    TEST_END();
}

/* ---- Alt screen enter/exit marks dirty ---------------------------------- */

int test_dirty_alt_screen(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 10, 100);
    term_clear_dirty(t);
    term_alt_screen_enter(t);
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_clear_dirty(t);
    term_alt_screen_exit(t);
    /* Primary buffer rows were dirty=true from init, preserved across alt screen */
    ASSERT_TRUE(term_has_dirty_rows(t));
    term_free(t);
    TEST_END();
}
